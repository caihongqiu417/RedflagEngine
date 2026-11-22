"""读取项目场景，编辑角色资产，并用本地 Redflag 构建检查结果。"""
from pathlib import Path
from tempfile import TemporaryDirectory
from threading import Lock
import json
import os
import shutil
import subprocess

from mcp.server import MCPServer
from mcp.server.mcpserver.exceptions import ToolError
from mcp.types import ToolAnnotations
from ruamel.yaml import YAML
import npc_assets
import image_assets
from project import ENGINE, PROJECT


ROOT = ENGINE
FORGE = PROJECT
SCENES = FORGE / "assets/scenes"
EDITOR = ROOT / "bin/Debug-windows-x86_64/RedflagForge/RedflagForge.exe"
app = MCPServer("redflag")
READ_ONLY = ToolAnnotations(readOnlyHint=True, openWorldHint=False)
SCENE_EDIT = Lock()
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
        if key == "Lights" and isinstance(item, list):
            result[key] = [str(light) for light in item]
            continue
        is_id = key.endswith("ID") or key == "Entity" or (key == "Value" and value.get("Type") == "Entity")
        result[key] = str(item) if is_id and type(item) is int else json_ids(item)
    return result


@app.tool(annotations=READ_ONLY)
def get_capabilities() -> dict[str, object]:
    """查看本机可用的检查工具、可编辑的资产和数据来源。"""
    return {"source": "saved project files", "project": FORGE.as_posix(), "scene_directory": "assets/scenes",
            "editor": EDITOR.relative_to(ROOT).as_posix(), "editor_exists": EDITOR.is_file(),
            "live_editor": False, "can_edit": True,
            "environment": {"source": "saved scene", "tools": ["inspect_environment", "set_zone_description"]},
            "editable_assets": ["environment zone descriptions", "UI images and source records"] +
                              (["NPC profile", "NPC behavior links", "NPC animation states"] if npc_assets.TOOL.is_file() else []),
            "npc_tool": npc_assets.TOOL.relative_to(ROOT).as_posix(), "npc_tool_exists": npc_assets.TOOL.is_file(),
            "check_backend": "Vulkan", "check_frames": 2}


# 美术离线工具

@app.tool()
def import_ui_image(collection: str, name: str, file: str, creator: str = "", prompt: str = "",
                    model: str = "", original: str = "", processing: str = "") -> dict[str, object]:
    """把本地 PNG 登记到当前项目 assets/ui/图集。已加工图片可另附原图路径与加工说明。"""
    try:
        return image_assets.import_png(FORGE, collection, name, file, creator, prompt, model, original, processing)
    except (OSError, ValueError) as error:
        raise ToolError(str(error)) from error


@app.tool()
def generate_ui_image(collection: str, name: str, prompt: str) -> dict[str, object]:
    """调用用户配置的 Images 服务，并把返回的 PNG 及提示词登记到当前项目。"""
    try:
        return image_assets.generate(FORGE, collection, name, prompt)
    except (OSError, ValueError) as error:
        raise ToolError(str(error)) from error


# 角色表和行为树

@app.tool(annotations=READ_ONLY)
def list_npcs() -> dict[str, list[str]]:
    """列出项目角色表中的文件名，可直接用于 inspect_npc。"""
    return {"files": npc_assets.list_roles()}


@app.tool(annotations=READ_ONLY)
def inspect_npc(npc: str) -> dict[str, object]:
    """查看已保存的角色设定、音色、节点及连线，以及每个候选在本机构建中走过的节点。"""
    return npc_assets.inspect(npc)


@app.tool()
def copy_npc(npc: str, new_file: str, display_name: str) -> dict[str, object]:
    """复制为独立角色。new_file 使用新的 *.npc.yaml 文件名，复制后的角色拥有独立的行为树。"""
    return npc_assets.copy(npc, new_file, display_name)


@app.tool()
def update_npc_role(npc: str, name: str | None = None, instructions: str | None = None,
                    leader_relationship: str | None = None) -> dict[str, object]:
    """修改角色名称、描述或与场景指定队友的约定，只写入此次提供的字段。传空字符串可以清除旧约定。"""
    return npc_assets.update(npc, {k: v for k, v in (("Name", name), ("Instructions", instructions),
                            ("LeaderRelationship", leader_relationship)) if v is not None})


@app.tool()
def set_npc_voice(npc: str, voice: str) -> dict[str, object]:
    """更换角色的 Voice 字段。可用音色取决于实际连接的语音服务。"""
    return npc_assets.update(npc, {"Voice": voice})


@app.tool()
def rewire_npc(npc: str, source: int, key: str, target: int) -> dict[str, object]:
    """将已有连线接到另一个节点。source 和 target 是图中的节点编号，key 是该连线的候选键。"""
    return npc_assets.update_links(npc, {(source, key): target})


@app.tool()
def set_npc_animation(npc: str, state: str, clip: str | None = None, loop: bool | None = None,
                      blend: float | None = None, contact: float | None = None, seconds: float | None = None) -> dict[str, object]:
    """按状态 ID 编辑动画。blend、seconds 以秒计，contact 是本段动作的接触时刻（0 到 1）。"""
    fields = {"Clip": clip, "Loop": loop, "Blend": blend, "Contact": contact, "Seconds": seconds}
    return npc_assets.edit_states(npc, {state: {k: v for k, v in fields.items() if v is not None}})


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
def inspect_environment(scene: str) -> dict[str, object]:
    """列出已保存的地形、环境区域和灯。实际运行中的亮度、遮挡和队友照片需在局内查看。"""
    entities = read_yaml(scene_file(scene))["Entities"]
    groups = {"terrains": [], "zones": [], "lights": []}
    kinds = {"TerrainComponent": "terrains", "EnvironmentZoneComponent": "zones",
             "PointLightComponent": "lights", "DirectionalLightComponent": "lights"}
    for entry in entities:
        for component, group in kinds.items():
            if component in entry:
                groups[group].append({"id": str(entry["Entity"]), "name": entry.get("TagComponent", {}).get("Tag", ""),
                                      "transform": entry.get("TransformComponent"), "component": component,
                                      "settings": json_ids(entry[component])})
    return {"scene": scene, "source": "saved scene", **groups}


@app.tool()
def set_zone_description(scene: str, entity: str, field: str, previous: str, text: str) -> dict[str, str]:
    """编辑区域的地貌、植被或地标。previous 填读到的原文，尚未填写的项传空字符串。"""
    if field not in ("Landform", "Vegetation", "Landmark"):
        raise ToolError("可修改地貌 Landform、植被 Vegetation 或地标 Landmark。")
    with SCENE_EDIT:
        return edit_zone_text(scene_file(scene), entity, field, previous, text)


def edit_zone_text(path: Path, entity: str, field: str, previous: str, text: str) -> dict[str, str]:
    original = npc_assets.read(path)
    parsed = YAML(typ="safe").compose(original)
    for item in npc_assets.fields(parsed)["Entities"].value:
        fields = npc_assets.fields(item)
        if fields["Entity"].value != entity or "EnvironmentZoneComponent" not in fields:
            continue
        node = fields["EnvironmentZoneComponent"]
        zone = npc_assets.fields(node)
        if (zone[field].value if field in zone else "") != previous:
            raise ToolError("区域说明已变化，请重新读取。")
        if field in zone:
            edited = npc_assets.replace_value(original, zone[field], text)
        else:
            attribute = field + ": " + json.dumps(text, ensure_ascii=False)
            if node.flow_style:
                at = node.end_mark.index - 1
                insert = (", " if zone else "") + attribute
            else:
                start = node.start_mark
                at = start.index
                newline = "\r\n" if "\r\n" in original else "\n"
                insert = attribute + newline + " " * start.column
            edited = original[:at] + insert + original[at:]
        npc_assets.replace_file(path, original, edited)
        return {"scene": path.name, "entity": entity, "field": field, "value": text}
    raise ToolError("所选实体没有环境区域组件。")


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
            source = FORGE / name
            if not source.is_dir():
                source = ROOT / "RedflagForge" / name
            shutil.copytree(source, work / name, copy_function=copy_asset)
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
