"""读取项目场景，编辑角色资产，并用本地 Redflag 构建检查结果。"""
from pathlib import Path
from tempfile import TemporaryDirectory
from threading import Lock
import json
import os
import shutil
import subprocess
import sys

from mcp.server import MCPServer
from mcp.server.mcpserver.exceptions import ToolError
from mcp.types import ToolAnnotations
from ruamel.yaml import YAML
import npc_assets
import image_assets
import playtest
import playtest_agent
import playtest_metrics
import playtest_workflow
import job_timeline
import asset_changes
import creation_reviews
from project import ENGINE, PROJECT


ROOT = ENGINE
sys.path.insert(0,str(ROOT / "scripts"))
from asset_bundle import AssetBundle
import dev_context
import context_review
FORGE = PROJECT
SCENES = FORGE / "assets/scenes"
EDITOR = ROOT / "bin/Debug-windows-x86_64/RedflagForge/RedflagForge.exe"
app = MCPServer("redflag")
READ_ONLY = ToolAnnotations(readOnlyHint=True, openWorldHint=False)
SCENE_EDIT = Lock()
SHARED_FORMATS = {".gltf", ".glb", ".obj", ".fbx", ".bin", ".ply", ".mtl", ".png", ".jpg",
                  ".jpeg", ".tga", ".bmp", ".hdr", ".ktx2", ".ttf", ".otf", ".dll"}


@app.tool()
def lookup_dev_context(query: str, operation: str = "find", task: str = "", limit: int = 4) -> dict[str, object]:
    """find 搜索公开接口。show 按完整名称或文件:行号找源码与可能调用处。填写 task 将本次返回留作审查。"""
    try:
        return dev_context.lookup(ROOT, FORGE, operation, query, limit, task)
    except (OSError, ValueError) as error:
        raise ToolError(str(error)) from error


@app.tool()
def begin_dev_task(intent: str, files: list[str]) -> dict[str, object]:
    """记录指定引擎源码当前的字节，以便和修改后比较。文件名相对于引擎仓库。"""
    try:
        return context_review.begin(ROOT, FORGE, intent, files)
    except (OSError, ValueError) as error:
        raise ToolError(str(error)) from error


@app.tool(annotations=READ_ONLY)
def review_dev_task(task: str = "") -> dict[str, object]:
    """按任务查引用、改动、测试原始输出和人工意见，留空列出最近任务。"""
    try:
        return context_review.review(ROOT, FORGE, task) if task else context_review.tasks(FORGE)
    except (OSError, ValueError) as error:
        raise ToolError(str(error)) from error


@app.tool()
def comment_dev_task(task: str, text: str) -> dict[str, object]:
    """给这次开发留下更正，Forge 和命令行都能看到。"""
    try:
        return context_review.note(FORGE, task, text)
    except (OSError, ValueError) as error:
        raise ToolError(str(error)) from error


@app.tool()
def begin_playtest(scene: str, intent: str, pose_nodes: list[str] | None = None) -> dict[str, object]:
    """在游戏窗口记录输入、状态和画面。检查动作时可填模型的骨骼名，逐帧记录这些关节在场景中的位置。"""
    path = scene_file(scene)
    return playtest.begin(FORGE, path.relative_to(FORGE).as_posix(), intent, pose_nodes=pose_nodes)


@app.tool(annotations=READ_ONLY)
def read_playtest(session: str, offset: int = 0, limit: int = 30) -> dict[str, object]:
    """查看当前操作权、最新状态与一段逐帧记录。把返回的 offset 用于下次增量读取。"""
    folder = playtest.run_folder(FORGE,session)
    return {**playtest.current(folder), **playtest.frames(folder,offset,limit)}


@app.tool()
def control_playtest(session: str, generation: int, op: str, key: str = "", owner: str = "",
                     x: float | None = None, y: float | None = None, lease_ms: int | None = None) -> dict[str, object]:
    """在本局按住、松开、点按、移动鼠标或转交操作权。返回原始命令和处理它的游戏帧。"""
    folder = playtest.run_folder(FORGE,session)
    if op == "handoff" and owner == "agent" and playtest.read_json(folder/"handoff.json")["owner"] == "human":
        raise ToolError("开发者正在操作，请在 Forge 中交还操作权。")
    command = playtest.issue(folder,generation,op,key,owner,x,y,lease_ms)
    return {"command":command, "receipt":playtest.await_receipt(folder,command["id"])}


@app.tool()
def comment_playtest(session: str, seq: int, text: str, entity: str = "") -> dict[str, object]:
    """针对某一帧或该帧中的角色留下反馈，Forge 回放也能看到。"""
    return playtest.feedback(playtest.run_folder(FORGE,session),seq,entity,text)


@app.tool()
def explore_playtest(session: str, profile: str, rounds: int = 4, use_images: bool = True) -> dict[str, object]:
    """用本机模型试玩几步。每步都观察新画面、角色状态和上次位移，交给人操作后立即结束。"""
    return playtest_agent.explore(FORGE,playtest.run_folder(FORGE,session),profile,rounds,see=use_images)


@app.tool(annotations=READ_ONLY)
def read_job_timeline(session: str, seq: int, baseline: str = "", baseline_seq: int | None = None) -> dict[str, object]:
    """读取选中帧以前最近收集的一批 CPU 任务。返回源码行、等待、依赖及沿依赖的执行时间，可另选一局对照。"""
    result = {"current": job_timeline.snapshot(playtest.run_folder(FORGE, session), seq)}
    if baseline:
        result["baseline"] = job_timeline.snapshot(playtest.run_folder(FORGE, baseline), seq if baseline_seq is None else baseline_seq)
    return result


@app.tool(annotations=READ_ONLY)
def evaluate_playtest(session: str, profile: str, baseline: str = "") -> dict[str, object]:
    """核对位移、事件先后、帧耗时和各类证据。给出有问题的帧，也可以按同一组规则读取旧局。"""
    checks = playtest_metrics.load_profile(FORGE,profile)
    result = {"current":playtest_metrics.assess(playtest.run_folder(FORGE,session),checks)}
    if baseline:
        result["baseline"] = playtest_metrics.assess(playtest.run_folder(FORGE,baseline),checks)
    return result


@app.tool()
def revise_playtest(session: str, profile: str, intent: str) -> dict[str, object]:
    """把试玩结果和作者意见交给模型，返回可以在 Forge 审查的资产修改。"""
    return playtest_workflow.propose_revision(FORGE,playtest.run_folder(FORGE,session),profile,intent,"http://127.0.0.1:8716")


@app.tool()
def iterate_playtest(scene: str, profile: str, intent: str, rounds: int = 4, revisions: int = 1) -> dict[str, object]:
    """自动试玩。发现问题时，允许模型在项目开放的字段内修订、保存，再从原场景开局验证。"""
    return playtest_workflow.iterate(FORGE,scene_file(scene).relative_to(FORGE).as_posix(),intent,profile,rounds,revisions)


@app.tool()
def propose_asset_change(intent: str, operations: list[dict]) -> dict[str, object]:
    """预览同一意图涉及的多个资产。每项给出 asset、field 路径、before 原值、after 新值。列表行用 Entity、Key 或 ID 选择。"""
    return asset_changes.propose(FORGE,intent,operations)


@app.tool()
def apply_asset_change(change: str, undo: bool = False) -> dict[str, object]:
    """保存或撤销这次创作修改。先核对全部目标字段，保留作者同时改动的其他字段。"""
    return asset_changes.apply(FORGE,change,undo)


def scene_files() -> dict[str, Path]:
    return {p.name: p for p in sorted(SCENES.glob("*.redflag"))
            if p.is_file() and not p.is_symlink() and p.resolve().parent == SCENES}


def scene_file(name: str) -> Path:
    path = scene_files().get(name)
    if path is None:
        raise ToolError("Unknown scene. Choose an exact name from list_scenes.")
    return path


@app.tool(annotations=READ_ONLY)
def scene_dependencies(scene: str, search: str = "", offset: int = 0, limit: int = 80) -> dict[str, object]:
    """从保存的场景追到模型、贴图、UI、NPC 定义和行为树。每项含直接使用它的文件，与打包器采用相同的引用。"""
    path = scene_file(scene)
    bundle = AssetBundle(FORGE)
    bundle.scene(path)
    query = search.casefold()
    edges = [{"asset":key,"used_by":sorted(refs)} for key,(_,refs) in sorted(bundle.files.items())
             if query in key.casefold() or any(query in ref.casefold() for ref in refs)]
    entities = [{"entity":str(e["Entity"]),"name":e.get("TagComponent",{}).get("Tag",""),
                 "components":[k for k in e if k.endswith("Component")]}
                for e in read_yaml(path).get("Entities",[])]
    return {"scene":scene,"count":len(edges),"assets":edges[max(0,offset):max(0,offset)+min(300,max(1,limit))],
            "entities":[e for e in entities if not search or query in json.dumps(e,ensure_ascii=False).casefold()]}


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
            "dev_context": ["lookup_dev_context", "begin_dev_task", "review_dev_task", "comment_dev_task"],
            "playtest":{"tools":["begin_playtest","read_playtest","control_playtest","explore_playtest","evaluate_playtest",
                                "comment_playtest","revise_playtest","iterate_playtest","read_job_timeline"],
                        "profiles": sorted(p.relative_to(FORGE/"assets/playtests").as_posix() for p in (FORGE/"assets/playtests").rglob("*.json")),
                        "human_control":"Forge / F6","native_events":"Redflag.Playtest.Record"},
            "environment": {"source": "saved scene", "tools": ["inspect_environment", "set_zone_description"]},
            "editable_assets": ["environment zone descriptions", "UI layouts", "UI images and source records"] +
                              (["NPC profile", "NPC behavior links", "NPC animation states"] if npc_assets.TOOL.is_file() else []),
            "npc_tool": npc_assets.TOOL.relative_to(ROOT).as_posix(), "npc_tool_exists": npc_assets.TOOL.is_file(),
            "check_backend": "Vulkan", "check_frames": 2}


# Forge 创作审查

@app.tool(annotations=READ_ONLY)
def list_creation_reviews() -> dict[str, object]:
    """列出当前项目中可在 Forge「窗口 → 创作审查」打开的案例。"""
    return {"cases": creation_reviews.cases(FORGE)}


@app.tool(annotations=READ_ONLY)
def get_creation_review(case: str) -> dict[str, object]:
    """读取意图、改动、复测和证据文件，并核对发布时记录的文件指纹。"""
    try:
        return creation_reviews.read(FORGE, case)
    except (OSError, ValueError) as error:
        raise ToolError(str(error)) from error


@app.tool()
def publish_creation_review(case: str, description: dict[str, object]) -> dict[str, object]:
    """收集当前项目验证目录中的真实文件，写出供人复查的记录。复测步骤是给作者阅读的文字。"""
    try:
        return creation_reviews.publish(FORGE, case, description)
    except (OSError, ValueError) as error:
        raise ToolError(str(error)) from error


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


def ui_file(name: str, extensions: tuple[str, ...]) -> Path:
    folder = (FORGE / "assets/ui").resolve()
    path = (FORGE / name).resolve()
    if not path.is_relative_to(folder) or path.suffix.lower() not in extensions or not path.is_file():
        raise ToolError("请选择当前项目 assets/ui 里的文件。")
    return path


@app.tool(annotations=READ_ONLY)
def list_ui_layouts() -> dict[str, list[str]]:
    """列出当前项目的 HUD 布局。返回的路径可以直接用于 inspect_ui_layout。"""
    layouts = []
    folder = FORGE / "assets/ui"
    for path in folder.rglob("*"):
        if path.suffix.lower() not in (".yml", ".yaml") or path.name.endswith(".rfsource.yaml"):
            continue
        if path.resolve().is_relative_to(folder) and "Elements" in read_yaml(path):
            layouts.append(path.relative_to(FORGE).as_posix())
    return {"layouts": sorted(layouts)}


@app.tool(annotations=READ_ONLY)
def inspect_ui_layout(layout: str) -> dict[str, object]:
    """读取界面中的文字字段、图片、位置、进度条和显示条件。"""
    path = ui_file(layout, (".yaml", ".yml"))
    values = read_yaml(path)
    if "Elements" not in values:
        raise ToolError("这份文件没有用 Elements 定义游戏界面。")
    return {"layout": path.relative_to(FORGE).as_posix(), **values}


@app.tool()
def set_ui_sprite(layout: str, key: str, previous: str, image: str) -> dict[str, str]:
    """替换 Sprites 里的一张图。previous 是上次读取的原值，image 使用从项目根目录开始的 PNG 路径。"""
    path = ui_file(layout, (".yaml", ".yml"))
    texture = ui_file(image, (".png",))
    value = Path(os.path.relpath(texture, path.parent)).as_posix()
    with SCENE_EDIT:
        original = npc_assets.read(path)
        root = npc_assets.fields(YAML(typ="safe").compose(original))
        sprites = npc_assets.fields(root["Sprites"]) if "Sprites" in root else {}
        if key not in sprites:
            raise ToolError("在这份布局的 Sprites 中找不到该名称。")
        if sprites[key].value != previous:
            raise ToolError("这张图的引用已经改变，请重新读取布局。")
        if previous != value:
            edited = npc_assets.replace_value(original, sprites[key], value)
            npc_assets.replace_file(path, original, edited)
    return {"layout": layout, "sprite": key, "value": value}


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
