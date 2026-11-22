"""验证稳定模型编号、glTF 外链、HUD 图片、角色、3DGS 拍摄图，以及失败后上一次包仍可用。"""
import hashlib
import json
from pathlib import Path
import sys
from tempfile import TemporaryDirectory
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from asset_bundle import AssetBundle


def main(work):
    project = work / "project"
    assets = project / "assets"
    def write(name, content):
        file = assets / name
        file.parent.mkdir(parents=True, exist_ok=True)
        file.write_text(content, encoding="utf-8")
        return file

    scene = write("scenes/test.redflag", """Scene: 测试
Entities:
  - Entity: 1
    ModelRendererComponent: {ModelID: 155, FilePath: assets/models/old.gltf}
    HudComponent: {Layout: assets/ui/hud.yaml}
    NpcComponent: {Definition: assets/ai/medic.npc.yaml}
""")
    write("models/new.gltf", json.dumps({"asset":{"version":"2.0"}, "buffers":[{"uri":"mesh.bin"}],
                                        "images":[{"uri":"brick%20wall.png"},{"uri":"data:image/png;base64,AA=="}]}))
    write("models/new.gltf.rfmeta", "RedflagAsset 1\n155 1\n")
    write("models/mesh.bin", "vertices")
    texture = write("models/brick wall.png", "texture")
    write("ui/hud.yaml", "Sprites: {button: Play.png}\n")
    write("ui/Play.png", "image")
    write("ai/medic.npc.yaml", "Tree: medic.tree.yaml\nAnimator: medic.animator.yaml\n")
    write("ai/medic.tree.yaml", "Nodes: []")
    write("ai/medic.animator.yaml", "States: []")
    index = write("baked/index.json", '{"views":[{"image":"angles/table.png"}]}')
    write("baked/angles/table.png", "view")
    write("baked/angles/scene.ply", "point cloud")
    archive = work/"game.zip"

    def build():
        bundle = AssetBundle(project)
        entry = bundle.scene(scene)
        bundle.views(index)
        return bundle.write(archive, entry)
    manifest = build()
    with zipfile.ZipFile(archive) as z:
        assert json.loads(z.read("asset-manifest.json")) == manifest
        for row in manifest["files"]:
            data = z.read(row["path"])
            assert hashlib.sha256(data).hexdigest() == row["sha256"] and len(data) == row["bytes"]
        assert {"assets/models/new.gltf.rfmeta","assets/models/mesh.bin","assets/models/brick wall.png",
                "assets/ui/Play.png","assets/ai/medic.tree.yaml","assets/ai/medic.animator.yaml",
                "assets/vision/baked/angles/scene.ply","assets/vision/baked/angles/table.png"} <= set(z.namelist())
        assert "assets/models/old.gltf" not in z.namelist()
    before = archive.read_bytes()
    texture.unlink()
    try: build()
    except FileNotFoundError: pass
    else: raise AssertionError("Missing glTF texture accepted")
    assert archive.read_bytes() == before
    print("PASS: native AssetManager lookup after move, glTF URI, HUD, actor, animator, views, hashes, failed rebuild")


if __name__ == "__main__":
    with TemporaryDirectory() as work: main(Path(work))
