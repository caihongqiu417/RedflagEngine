"""Forge 应用曝光草稿时只改场景渲染配置里的 ExposureEV。"""
from datetime import datetime, timezone
from uuid import uuid4

from ruamel.yaml import YAML
from ruamel.yaml.error import YAMLError
from ruamel.yaml.nodes import MappingNode

import asset_changes
from npc_assets import same_value
from playtest import write_json, writer_lock
from playtest_metrics import assess_exposure


def exposure_value(number):
    # 与 LightingPreview 的曝光滑块一致。增加 1 EV 会让色调映射前的亮度翻倍。
    if type(number) not in (int, float) or not -4 <= number <= 4:
        raise ValueError("曝光补偿应为 −4 到 +4 EV 的有限数字。")
    return float(number)


def read_exposure(text):
    yaml = YAML(typ="safe")
    try:
        settings = yaml.load(text)
        tree = yaml.compose(text)
    except YAMLError as error:
        raise ValueError("渲染配置的 YAML 有误，请检查重复字段和缩进。") from error
    if not isinstance(tree, MappingNode) or not isinstance(settings, dict):
        raise ValueError("渲染配置应是一组设置，请检查源文件。")

    node = None
    if "ExposureEV" in settings:
        # 字段来自合并表、或和别的字段共用 YAML 别名时，直接替换会连带改动别处。
        node = asset_changes.selected_node(tree, ["ExposureEV"], asset_changes.shared_nodes(tree))
    return tree, node, exposure_value(settings.get("ExposureEV", 0))


def apply_exposure(profile, original, value):
    if not profile.is_absolute() or not profile.name.casefold().endswith(".redflag.render.yaml"):
        raise ValueError("请选择当前打开的 .redflag 场景旁的渲染配置。")
    scene = profile.with_name(profile.name[:-len(".render.yaml")])
    if not scene.is_file() or not profile.is_file() or scene.is_symlink() or profile.is_symlink():
        raise ValueError("请确认场景和同名渲染配置都是已经保存的常规文件。")

    path = profile.resolve()
    assets = next((p for p in path.parents if p.name.casefold() == "assets"), None)
    project = assets.parent if assets else path.parent
    original, value = exposure_value(original), exposure_value(value)
    root = project / "captures/changes"
    root.mkdir(parents=True, exist_ok=True)

    with writer_lock(root):
        # 项目资产与通用创作工具共用事务。单独打开的场景在自己的目录下保存复查记录。
        if assets:
            asset_changes.recover(project, root)
        previous = path.read_bytes()
        text = previous.decode("utf8")
        tree, node, current = read_exposure(text)
        if same_value(current, value):
            return {"profile": str(path)}
        if not same_value(current, original):
            raise ValueError(f"曝光已被其他作者改为 {current:g} EV，请重新读取场景设置。")

        if node is None:
            edited = asset_changes.insert_optional(text, tree, "ExposureEV", value)
        else:
            edited = asset_changes.replace_value(text, node, current, value, ())
        if not same_value(read_exposure(edited)[2], value):
            raise ValueError("无法按当前排版写入曝光，请检查源文件。")
        asset_changes.replace_bytes(path, previous, edited.encode("utf8"))

        key = uuid4().hex
        record = {"id": key, "asset": path.relative_to(project).as_posix(), "field": "ExposureEV",
                  "before": current, "after": value, "before_absent": node is None,
                  "applied": datetime.now(timezone.utc).isoformat()}
        reply = {"profile": str(path), "id": key}
        try:
            recorded = read_exposure(path.read_bytes().decode("utf8"))[2]
            record["assessment"] = assess_exposure(recorded, value)
        except (OSError, ValueError) as error:
            record["assessment"] = {"verdict": "findings", "findings": [
                {"kind": "exposure_read_error", "message": str(error)}]}
        if record["assessment"]["findings"]:
            reply["warning"] = "曝光已应用，复查时发现问题。" + record["assessment"]["findings"][0]["message"]

        report = root / "exposure" / (key + ".json")
        try:
            report.parent.mkdir(exist_ok=True)
            write_json(report, record)
            reply["record"] = str(report)
        except OSError as error:
            reply.setdefault("warning", "曝光已应用，无法保存复查记录。" + str(error))
        return reply
