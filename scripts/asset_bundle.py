"""从一个场景收集游戏实际引用的文件，保留引擎使用的相对路径与 .rfmeta 编号。"""
import argparse
import hashlib
import json
import os
import struct
import subprocess
import zipfile
from pathlib import Path
from tempfile import NamedTemporaryFile
from urllib.parse import unquote, urlsplit

from ruamel.yaml import YAML


MAPS = {"BaseColorMap", "NormalMap", "MetallicRoughnessMap", "AOMap", "AlphaMap", "EmissiveMap", "HeightMap", "TexturePath"}
UI_FILES = {".png", ".jpeg", ".jpg", ".ttf", ".otf", ".svg", ".webp", ".yaml", ".dds", ".ktx2"}
INDEX = Path(__file__).resolve().parents[1] / "bin-int/Tools/Assets/AssetIndex.exe"


def values(data):
    if isinstance(data, dict):
        for k, v in data.items():
            yield k, v
            yield from values(v)
    elif isinstance(data, list):
        for v in data:
            yield from values(v)


def gltf(path: Path):
    if path.suffix.lower() == ".gltf":
        return json.loads(path.read_bytes())
    with path.open("rb") as stream:
        magic, version, _, length, kind = struct.unpack("<4sIIII", stream.read(20))
        if magic != b"glTF" or version != 2 or kind != 0x4E4F534A:
            raise ValueError(f"无效的 glTF 文件 {path}")
        return json.loads(stream.read(length))


class AssetBundle:
    def __init__(self, project: Path):
        self.root = project.resolve()
        self.files = {}

    def model_paths(self, scene):
        ids = {value for field, value in values(scene) if field == "ModelID" and value}
        if not ids:
            return {}
        if not INDEX.is_file():
            raise ValueError("请先从仓库根目录运行 scripts/build_asset_index.cmd。")
        result = subprocess.run([INDEX, self.root / "assets"], input="".join(f"{i}\n" for i in sorted(ids)),
                                encoding="utf-8", capture_output=True)
        if result.returncode:
            raise ValueError(result.stderr.strip())
        return {int(number): Path(path) for number, path in (row.split("\t", 1) for row in result.stdout.splitlines())}

    def add(self, path: Path, owner: str, name: str | None = None):
        path = path.resolve()
        name = name or path.relative_to(self.root).as_posix()
        if not path.is_file():
            raise FileNotFoundError(f"{owner} 引用了缺失文件 {path}")
        if name in self.files:
            if path != self.files[name][0]:
                raise ValueError(f"两个源文件都要保存成 {name}")
            self.files[name][1].add(owner)
            return
        self.files[name] = (path, {owner})
        if path.suffix.lower() in UI_FILES | {".glb", ".gltf", ".fbx", ".obj"}:
            for credit in (*path.parent.glob("LICENSE*"), path.parent / "README.md",
                           path.parent / "sources.json", path.parent / "surface-source.json"):
                if credit.is_file() and path.is_relative_to(self.root):
                    self.add(credit, name)
        meta = path.with_name(path.name + ".rfmeta")
        if meta.is_file():
            self.add(meta, name, name + ".rfmeta")
        if path.suffix.lower() == ".png":
            credit = path.with_name(path.name + ".rfsource.yaml")
            if credit.is_file():
                self.add(credit, name, name + ".rfsource.yaml")
        if path.suffix.lower() in (".gltf", ".glb"):
            model = gltf(path)
            for entry in model.get("buffers", []) + model.get("images", []):
                uri = entry.get("uri")
                if not uri or uri.startswith("data:"):
                    continue
                url = urlsplit(uri)
                if url.scheme or url.netloc:
                    raise ValueError(f"{name} 包含非本地资源 {uri}")
                self.add(path.parent / unquote(url.path), name)
        elif path.name.endswith(".npc.yaml"):
            npc = YAML(typ="safe").load(path.read_bytes())
            for key in ("Tree", "Animator"):
                if npc.get(key):
                    self.add(path.parent / npc[key], name)
        elif path.suffix.lower() in (".yaml", ".yml") and name.startswith("assets/ui/") and not name.endswith(".rfsource.yaml"):
            # 图集、图标和字体沿布局文件所在目录读取。
            for _, value in values(YAML(typ="safe").load(path.read_bytes())):
                if isinstance(value, str) and Path(value).suffix.lower() in UI_FILES:
                    self.add(path.parent / value, name)

    def scene(self, scene: Path):
        scene = scene.resolve()
        name = scene.relative_to(self.root).as_posix()
        self.add(scene, "scene")
        data = YAML(typ="safe").load(scene.read_bytes())
        models = self.model_paths(data)
        for field, value in values(data):
            if field == "ModelRendererComponent":
                for part in (value, *value.get("LODs", [])):
                    if part.get("ModelID"): self.add(models[part["ModelID"]], name)
                    elif part.get("FilePath"): self.add(self.root / part["FilePath"], name)
            elif field == "DecalComponent" and value.get("Texture"):
                self.add(self.root / value["Texture"], name)
            elif field == "FoliageComponent" and value.get("Profile"):
                self.add(self.root / value["Profile"], name)
            elif field == "TerrainComponent":
                for image in ("Heightmap", "BaseColorTexture"):
                    if value.get(image): self.add(self.root / value[image], name)
            elif field == "HudComponent" and value.get("Layout"):
                self.add(self.root / value["Layout"], name)
            elif field == "SkeletonAnimationComponent" and value.get("Idle"):
                self.add(scene.parent / value["Idle"], name)
            elif field in ("Definition", *MAPS) and isinstance(value, str) and value:
                self.add(self.root / value, name)
            elif field in ("Environment", "File") and isinstance(value, str) and value:
                self.add(scene.parent / value, name)
        for suffix in (".render.yaml", ".rfprobes"):
            sidecar = scene.with_name(scene.name + suffix)
            if sidecar.is_file(): self.add(sidecar, name)
        return name

    def views(self, index: Path):
        index = index.resolve()
        target = "assets/vision/" + index.parent.name
        self.add(index, "3DGS views", target + "/index.json")
        for view in json.loads(index.read_bytes())["views"]:
            image = (index.parent / view["image"]).resolve()
            relative = image.relative_to(index.parent).as_posix()
            self.add(image, target + "/index.json", target + "/" + relative)
            cloud = image.parent / "scene.ply"
            if cloud.is_file():
                self.add(cloud, target + "/index.json", target + "/" + cloud.relative_to(index.parent).as_posix())

    def runtime(self, executable: Path):
        self.add(executable, "player", "RedflagForge.exe")
        self.add(Path(__file__).with_name("Play-Bundle.cmd"), "player", "Play.cmd")
        for file in executable.parent.glob("*.dll"):
            self.add(file, "player", file.name)
        for library in (self.root / "Resources/Scripts").glob("*.dll"):
            self.add(library, "player")
        for folder in ("mono/lib", "mono/etc", "assets/shaders"):
            for file in (self.root / folder).rglob("*"):
                if file.is_file(): self.add(file, "player")

    def companion(self):
        from package_ai_sdk import SCRIPTS
        for name in SCRIPTS:
            self.add(Path(__file__).with_name(name), "companion", "scripts/" + name)
        self.add(Path(__file__).with_name("Play-With-Companion.cmd"), "companion", "Play-With-Companion.cmd")

    def write(self, output: Path, entry: str):
        output.parent.mkdir(parents=True, exist_ok=True)
        with NamedTemporaryFile(dir=output.parent, delete=False) as f:
            temporary = Path(f.name)
        manifest = {"format": 1, "scene": entry, "files": []}
        try:
            with zipfile.ZipFile(temporary, "w", zipfile.ZIP_DEFLATED, compresslevel=4) as archive:
                for name, (source, owners) in sorted(self.files.items()):
                    digest = hashlib.sha256()
                    size = 0
                    with source.open("rb") as inp, archive.open(name, "w") as out:
                        while chunk := inp.read(1024 * 1024):
                            out.write(chunk); digest.update(chunk); size += len(chunk)
                    manifest["files"].append({"path": name, "bytes": size, "sha256": digest.hexdigest(), "used_by": sorted(owners)})
                archive.writestr("asset-manifest.json", json.dumps(manifest, ensure_ascii=False, indent=2))
            os.replace(temporary, output)
        finally:
            temporary.unlink(missing_ok=True)
        return manifest


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("scene", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--project", type=Path, default=Path("RedflagForge"))
    parser.add_argument("--ui", type=Path, action="append", default=[], help="另行选用的 UI 布局文件")
    parser.add_argument("--views", type=Path, action="append", default=[], help="3DGS 烘焙的 index.json")
    parser.add_argument("--runtime", type=Path, help="一起打包指定 RedflagForge.exe 及游戏运行库")
    parser.add_argument("--companion", action="store_true", help="随包提供本机模型和语音服务的启动入口")
    parser.add_argument("--guide", type=Path, help="随包提供这款游戏的操作手册")
    args = parser.parse_args()
    bundle = AssetBundle(args.project)
    entry = bundle.scene(args.scene)
    for ui in args.ui: bundle.add(ui, entry)
    for index in args.views: bundle.views(index)
    if args.runtime: bundle.runtime(args.runtime)
    if args.companion: bundle.companion()
    if args.guide: bundle.add(args.guide, "guide", args.guide.name)
    manifest = bundle.write(args.output, entry)
    print(f"{entry} -> {args.output}: {len(manifest['files'])} files")
