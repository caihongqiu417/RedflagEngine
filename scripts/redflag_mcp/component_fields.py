"""从保存的场景读取组件。字段名、类型、单位和限制由引擎和 Forge 共用。"""
from ctypes import c_float
import json
import math

from ruamel.yaml import YAML

from project import ENGINE


SCHEMA = ENGINE / "Redflag/src/Redflag/Scene/ComponentFields.json"
DEFAULTS = ENGINE / "output/api/component-defaults.json"


def component_types():
    return {item["name"]: item for item in json.loads(SCHEMA.read_bytes())["components"]}


def find_entity(scene, entity):
    found = [row for row in scene.get("Entities", []) if str(row["Entity"]) == str(entity)]
    if len(found) != 1:
        raise ValueError(f"找不到唯一的实体 {entity}。请重新读取场景。")
    return found[0]


def saved_fields(row, component):
    if component not in row or not isinstance(row[component], dict):
        raise ValueError(f"实体 {row['Entity']} 没有 {component}。")
    return row[component]


def defaults(component):
    return json.loads(DEFAULTS.read_bytes())[component]


def checked_number(value, field):
    if type(value) not in (int, float):
        raise ValueError(f"{field['name']} 需要数字。")
    try:
        number = c_float(value).value
    except OverflowError:
        number = math.inf
    if not math.isfinite(number) or (field.get("positive") and number <= 0):
        raise ValueError(f"{field['name']} 需要能由引擎表示的有限数。{field.get('constraint', '')}")
    return number


def check_value(value, field):
    kind = field["type"]
    if kind == "float":
        return checked_number(value, field)
    if kind == "vec3":
        if not isinstance(value, list) or len(value) != 3:
            raise ValueError(f"{field['name']} 需要按 X、Y、Z 填三个数。")
        return [checked_number(x, field) for x in value]
    if kind == "enum" and value in field["choices"]:
        return value
    raise ValueError(f"{field['name']} 的选项为 {field.get('choices', [])}。")


def validate(component, values):
    checked = {}
    for field in component["fields"]:
        key = field["name"]
        if key in values:
            value = values[key]
        elif field.get("optional"):
            value = defaults(component["name"])[key]
        else:
            raise ValueError(f"{component['name']} 缺少场景必填项 {key}。")
        checked[key] = check_value(value, field)

    pair = component.get("diameter")
    if pair and checked[pair["height"]] < 2 * checked[pair["radius"]]:
        raise ValueError("胶囊的总长至少要等于直径（半径的两倍）。可以一起修改半径和总长。")


def tuning_notes(path, entity, component, script):
    sidecar = path.with_suffix(".tuning.yaml")
    if not sidecar.is_file():
        return {}
    data = YAML(typ="safe").load(sidecar.read_bytes())
    return {n["field"]: {"reason": n["reason"], "evidence": n.get("evidence", [])}
            for n in data["notes"] if n["component"] == component and int(entity) in n["entities"]
            and ("script" not in n or n["script"] == script)}


def inspect(project, path, entity, component, proposed=None):
    scene = YAML(typ="safe").load(path.read_bytes())
    row = find_entity(scene, entity)
    saved = saved_fields(row, component)
    notes = tuning_notes(path, entity, component, saved.get("Class"))
    asset = path.relative_to(project).as_posix()
    route = ["Entities", {"Entity": str(row["Entity"])}, component]
    proposed = proposed or {}

    reply = {"asset": asset, "entity": str(row["Entity"]), "name": row.get("TagComponent", {}).get("Tag", ""),
             "component": component, "scope": "saved_scene", "fields": [], "operations": []}
    if component == "ScriptComponent":
        # Mono 负责脚本反射；资产工具只读出作者在场景中保存的脚本字段和类型。
        reply["script"] = saved["Class"]
        fields = []
        for entry in saved.get("ScriptFields", []):
            key = entry["Name"]
            fields.append({"name": key, "type": entry["Type"], "value": entry["Value"], "source": "scene",
                           "field": route + ["ScriptFields", {"Name": key}, "Value"]})
    else:
        spec = component_types().get(component)
        if spec is None:
            raise ValueError(f"引擎尚未公开 {component} 的字段。")
        validate(spec, {**saved, **proposed})
        initial = defaults(component)
        reply["label"] = spec["label"]
        fields = []
        for info in spec["fields"]:
            key = info["name"]
            fields.append({**info, "value": saved.get(key, initial[key]), "default": initial[key],
                           "source": "scene" if key in saved else "native_default", "field": route + [key]})

    unknown = proposed.keys() - {f["name"] for f in fields}
    if unknown:
        raise ValueError(f"找不到字段 {', '.join(sorted(unknown))}。")
    for field in fields:
        key = field["name"]
        if key in notes:
            field["tuning"] = notes[key]
        if key in proposed:
            if component == "ScriptComponent":
                check_script_value(field["type"], key, proposed[key])
            field["proposed"] = proposed[key]
            reply["operations"].append({"asset": asset, "field": field["field"], "before": field["value"],
                                        "after": proposed[key]})
            if field.get("optional"):
                reply["operations"][-1]["before_absent"] = field["source"] == "native_default"
            if component == "ScriptComponent":
                reply["operations"][-1]["script"] = reply["script"]
                reply["operations"][-1]["script_type"] = field["type"]
    reply["fields"] = fields
    return reply


def check_script_value(script_type, name, value):
    # 脚本的 Float / Vector3 沿用场景中记录的类型，项目里的数值说明只供查阅。
    kind = {"Float": "float", "Vector3": "vec3"}.get(script_type)
    if kind is None:
        raise ValueError(f"此入口目前支持预览脚本的 Float、Vector3 字段，{name} 的类型为 {script_type}。")
    check_value(value, {"name": name, "type": kind})
