"""Redflag 的本地 MCP，只接受场景目录里已有的场景名称。"""
from pathlib import Path
from tempfile import TemporaryDirectory
import os
import shutil
import subprocess

from mcp.server import MCPServer
from mcp.server.mcpserver.exceptions import ToolError
from mcp.types import ToolAnnotations
from ruamel.yaml import YAML


ROOT = Path(__file__).resolve().parents[2]
FORGE = ROOT / "RedflagForge"
SCENES = FORGE / "assets/scenes"
EDITOR = ROOT / "bin/Debug-windows-x86_64/RedflagForge/RedflagForge.exe"
app = MCPServer("redflag")
READ_ONLY = ToolAnnotations(readOnlyHint=True, openWorldHint=False)
SHARED_FORMATS = {".gltf", ".glb", ".obj", ".fbx", ".bin", ".ply", ".mtl", ".png", ".jpg",
                  ".jpeg", ".tga", ".bmp", ".hdr", ".ktx2", ".ttf", ".otf", ".dll"}


def scene_files() -> dict[str, Path]:
    return {p.name: p for p in sorted(SCENES.glob("*.redflag"))
            if p.is_file() and not p.is_symlink() and p.resolve().parent == SCENES}


def scene_file(name: str) -> Path:
    path = scene_files().get(name)
    if path is None:
        raise ToolError("Unknown scene. Choose an exact name from list_scenes.")
    return path


def sidecar(scene: Path, suffix: str) -> Path:
    path = scene.with_name(scene.name + suffix)
    if path.is_symlink() or path.resolve().parent != SCENES:
        raise ToolError("The scene sidecar must be a regular project file.")
    return path


def read_yaml(path: Path) -> dict:
    with path.open(encoding="utf-8-sig") as stream:
        data = YAML(typ="safe").load(stream)
    if not isinstance(data, dict):
        raise ToolError(f"Expected a YAML mapping in {path.name}.")
    return data


def json_ids(value):
    # JavaScript 的 number 无法精确保存全部 64 位编号。普通数值仍按原类型返回。
    if isinstance(value, list):
        return [json_ids(item) for item in value]
    if not isinstance(value, dict):
        return value
    result = {}
    for key, item in value.items():
        is_id = key.endswith("ID") or key == "Entity" or (key == "Value" and value.get("Type") == "Entity")
        result[key] = str(item) if is_id and type(item) is int else json_ids(item)
    return result


@app.tool(annotations=READ_ONLY)
def get_capabilities() -> dict[str, object]:
    """说明数据来源、可执行文件及验证范围。查看已保存的文件无需启动编辑器。"""
    return {"source": "saved project files", "scene_directory": "RedflagForge/assets/scenes",
            "editor": EDITOR.relative_to(ROOT).as_posix(), "editor_exists": EDITOR.is_file(),
            "live_editor": False, "can_edit": False, "check_backend": "Vulkan", "check_frames": 2}


@app.tool(annotations=READ_ONLY)
def list_scenes() -> dict[str, list[str]]:
    """列出已保存的 .redflag 场景名称；可以直接把其中一个名称传给其他工具。"""
    return {"scenes": list(scene_files())}


@app.tool(annotations=READ_ONLY)
def inspect_scene(scene: str, offset: int = 0, limit: int = 50) -> dict[str, object]:
    """查看天空、环境图和实体组件。offset 从 0 开始，limit 为 1 到 100。"""
    if offset < 0 or not 1 <= limit <= 100:
        raise ToolError("Use an offset >= 0 and a limit between 1 and 100.")
    data = read_yaml(scene_file(scene))
    entities = data.get("Entities") or []
    page = []
    for entity in entities[offset:offset + limit]:
        page.append({"id": str(entity["Entity"]), "name": entity.get("TagComponent", {}).get("Tag", ""),
                     "components": {k: json_ids(v) for k, v in entity.items() if k.endswith("Component") and k != "TagComponent"}})
    return {"scene": scene, "title": data["Scene"], "sky_visible": data.get("SkyVisible", True),
            "environment": data.get("Environment"), "total_entities": len(entities), "entities": page,
            "next_offset": offset + limit if offset + limit < len(entities) else None}


@app.tool(annotations=READ_ONLY)
def get_render_settings(scene: str) -> dict[str, object]:
    """读取场景旁的 .render.yaml。缺少文件时返回空设置，未写的项目沿用引擎默认值。"""
    path = sidecar(scene_file(scene), ".render.yaml")
    if not path.is_file():
        return {"scene": scene, "file": None, "settings": {}}
    return {"scene": scene, "file": path.name, "settings": read_yaml(path)}


def copy_asset(source, destination):
    # 启动检查只读取模型、贴图、字体和程序集。场景、元数据及其他可写文件保留独立副本。
    if Path(source).suffix.lower() in SHARED_FORMATS:
        try:
            os.link(source, destination)
            return
        except OSError:
            pass  # 文件系统不支持硬链接时仍可运行。
    shutil.copy2(source, destination)


@app.tool()
def check_scene(scene: str) -> dict[str, object]:
    """用独立的 Debug Vulkan 编辑器打开已保存的场景副本，运行两帧并返回退出码和日志摘录。"""
    path = scene_file(scene)
    result = {"scene": scene, "frames_requested": 2, "exit_code": None, "diagnostics": []}
    if not EDITOR.is_file():
        return {**result, "status": "missing_editor", "diagnostics": ["Build RedflagForge for Debug x64 first."]}
    scratch = ROOT / "bin-int/redflag-mcp-runs"
    scratch.mkdir(parents=True, exist_ok=True)
    with TemporaryDirectory(prefix="check-", dir=scratch) as directory:
        work = Path(directory)
        # 编辑器会在当前目录保存 ImGui 布局、计时记录和资源缓存。在副本里启动也保护了模型旁的元数据。
        for name in ("Resources", "mono"):
            shutil.copytree(FORGE / name, work / name, copy_function=copy_asset)
        shutil.copytree(FORGE / "assets", work / "assets", ignore=shutil.ignore_patterns("scenes", "cache"),
                        copy_function=copy_asset)
        scenes = work / "assets/scenes"
        scenes.mkdir()
        shutil.copyfile(path, scenes / scene)
        for suffix in (".render.yaml", ".rfprobes"):
            file = sidecar(path, suffix)
            if file.is_file():
                shutil.copyfile(file, scenes / file.name)

        command = [str(EDITOR), "--hidden", "--vulkan", "--scene", str(scenes / scene), "--frames", "2"]
        try:
            run = subprocess.run(command, cwd=work, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT, timeout=120, creationflags=subprocess.CREATE_NO_WINDOW)
        except subprocess.TimeoutExpired:
            return {**result, "status": "timeout", "diagnostics": ["The editor did not finish within 120 seconds."]}
        lines = run.stdout.decode("utf-8", errors="replace").splitlines()
        markers = ("opened ", "lighting preview", "cannot", "could not", "missing", "failed", "error", "warning")
        diagnostics = [line for line in lines if any(word in line.lower() for word in markers)]
        if run.returncode != 0:
            diagnostics.extend(line for line in lines[-5:] if line not in diagnostics)
        opened = any(line.endswith("Editor: Opened " + (scenes / scene).as_posix()) for line in lines)
        return {**result, "status": "completed" if run.returncode == 0 and opened else "failed",
                "exit_code": run.returncode, "diagnostics": diagnostics[-20:]}


if __name__ == "__main__":
    app.run(transport="stdio")
