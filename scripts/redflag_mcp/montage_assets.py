"""让 Agent 调整一个动作通知的时刻，保留同一文件中手工编辑过的其他内容。"""
from ruamel.yaml import YAML
from animation_transitions import asset, validate
from npc_assets import ToolError, fields, read, replace_file, replace_value, same_value


def check(path):
    validate(path, "validate-montage")


def inspect(name):
    path = asset(name, ".montage.yaml")
    check(path)
    return YAML(typ="safe").load(read(path))


def set_event_time(name, marker, field, original, seconds):
    if field not in ("At", "Start", "End"):
        raise ToolError("时刻通知使用 At，区间使用 Start 或 End，单位为秒。")
    path = asset(name, ".montage.yaml")
    before = read(path)
    yaml = YAML(typ="safe")
    events = yaml.load(before).get("Events", [])
    i = next((i for i, event in enumerate(events) if event["Name"] == marker), None)
    if i is None or field not in events[i]:
        raise ToolError(f"动作里没有通知 {marker} 的 {field} 字段。")
    current = events[i][field]
    if not same_value(current, seconds):
        if not same_value(current, original):
            raise ToolError("这个时刻已被其他作者修改。请重新读取通知。")
        row = fields(yaml.compose(before))["Events"].value[i]
        after = replace_value(before, fields(row)[field], seconds)
        replace_file(path, before, after, check)
    return {"file": name, "event": marker, "field": field, "seconds": seconds}
