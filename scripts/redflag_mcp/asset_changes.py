"""按字段审查、保存和撤销一次创作修改。每次只替换选中的值，保留作者在其他位置的修改。"""
from collections import defaultdict
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
from tempfile import NamedTemporaryFile
from uuid import uuid4

from ruamel.yaml import YAML
from ruamel.yaml.nodes import MappingNode, ScalarNode, SequenceNode

import component_fields
from playtest import read_json, write_json, writer_lock
from project import PROJECT


def project_file(project, name):
    path = (project / name).resolve()
    if not path.is_relative_to(project / "assets") or path.suffix.lower() not in (".redflag",".yaml",".yml",".json") or not path.is_file():
        raise ValueError(f"请选择当前项目中已有的场景或配置资产：{name}")
    return path


def shared_nodes(tree):
    seen, shared = set(), set()
    stack = [tree]
    while stack:
        node = stack.pop()
        if id(node) in seen:
            shared.add(id(node))
            continue
        seen.add(id(node))
        if isinstance(node, MappingNode):
            stack.extend(v for _, v in node.value)
        elif isinstance(node, SequenceNode):
            stack.extend(node.value)
    return shared


def selected_node(tree, route, shared):
    node = tree
    if not route:
        raise ValueError("请选择要修改的字段。")
    for part in route:
        if isinstance(node,MappingNode) and isinstance(part,str):
            node = next((v for k,v in node.value if k.value == part),None)
        elif isinstance(node,SequenceNode) and isinstance(part,dict) and len(part) == 1:
            # 场景实体、候选和行为树节点可以被作者重新排序，依照 Entity、Key、ID 等稳定字段查找。
            key,value = next(iter(part.items()))
            rows = [row for row in node.value if isinstance(row,MappingNode) and
                    any(k.value == key and v.value == str(value) for k,v in row.value)]
            node = rows[0] if len(rows) == 1 else None
        else:
            node = None
        if node is None:
            raise ValueError(f"找不到唯一字段 {part}。请重新读取资产。")
        if id(node) in shared:
            raise ValueError("这个值通过 YAML 别名被多个位置引用，请先为要单独修改的实体保存独立值。")
    return node


def selected(tree, route):
    node = selected_node(tree, route, ())
    if not isinstance(node, ScalarNode):
        raise ValueError("请选择一个文字、数字或开关字段。")
    return node


def same_value(a, b):
    if isinstance(a, list) and isinstance(b, list):
        return len(a) == len(b) and all(same_value(x, y) for x, y in zip(a, b))
    if type(a) in (int, float) and type(b) in (int, float):
        return a == b
    return type(a) is type(b) and a == b


def replace_value(text, node, current, value, shared):
    if same_value(current, value):
        return text
    if isinstance(node, SequenceNode) and isinstance(value, list) and len(node.value) == len(value):
        # 逐个替换向量坐标，保留三个坐标之间原有的换行与注释。
        for child, before, number in reversed(list(zip(node.value, current, value))):
            if id(child) in shared:
                raise ValueError("这个向量的坐标被其他字段引用，请先分开保存。")
            text = replace_value(text, child, before, number, shared)
        return text
    if not isinstance(node, ScalarNode) or isinstance(value, (dict, list)):
        raise ValueError("请选择一个标量或一个已登记的向量字段。")
    start, end = node.start_mark.index, node.end_mark.index
    replacement = json.dumps(value, ensure_ascii=False, allow_nan=False)
    if text[start:end].endswith("\n"):
        replacement += "\r\n" if text[start:end].endswith("\r\n") else "\n"
    return text[:start] + replacement + text[end:]


def insert_optional(text, parent, key, value):
    first = parent.value[0][0].start_mark
    newline = "\r\n" if "\r\n" in text else "\n"
    separator = ", " if parent.flow_style else newline + " " * first.column
    entry = key + ": " + json.dumps(value, allow_nan=False) + separator
    return text[:first.index] + entry + text[first.index:]


def remove_optional(text, parent, key):
    name, value = next((k, v) for k, v in parent.value if k.value == key)
    start, end = name.start_mark.index, value.end_mark.index
    if parent.flow_style:
        comma = re.match(r"[ \t]*,[ \t]?", text[end:])
        if comma:
            end += comma.end()
        else:
            comma = re.search(r",[ \t]*$", text[:start])
            if not comma:
                raise ValueError("这项的排版已手动修改，请检查后撤销。")
            start = comma.start()
    else:
        line = text.rfind("\n", 0, start) + 1
        newline = re.match(r"[ \t]*\r?\n", text[end:])
        if text[line:start].strip() or not newline:
            raise ValueError("这项的同一行还有手工内容，请检查后撤销。")
        start, end = line, end + newline.end()
    return text[:start] + text[end:]


def check_script_edit(tree, shared, op, value, proposing):
    route = op["field"]
    script = selected_node(tree, route[:3] + ["Class"], shared).value
    entry = selected_node(tree, route[:-1], shared)
    kind = next(v.value for k, v in entry.value if k.value == "Type")
    if op.get("script", script) != script or op.get("script_type", kind) != kind:
        raise ValueError("这个实体的脚本或字段类型已变，请重新读取场景。")

    if proposing:
        op["script"] = script
        op["script_type"] = kind
    if kind in ("Float", "Vector3"):
        component_fields.check_script_value(kind, str(route[-2]), value)


def rewrite(path, contents, operations, reverse, proposing):
    text = contents.decode("utf8")
    yaml = YAML(typ="safe")
    specs = component_fields.component_types() if path.suffix == ".redflag" else {}
    checked = set()
    for op in operations:
        field = op["field"]
        expected, value = (op["after"], op["before"]) if reverse else (op["before"], op["after"])
        tree = yaml.compose(text)
        shared = shared_nodes(tree)
        scene_field = len(field) >= 4 and field[0] == "Entities" and isinstance(field[1], dict)
        spec = specs.get(field[2]) if scene_field and isinstance(field[2], str) else None

        # 一份旧场景可能使用引擎的缺省字段。撤销时要恢复省略，保留以后改默认值的含义。
        if spec:
            if len(field) != 4 or set(field[1]) != {"Entity"}:
                raise ValueError("请用实体编号和完整字段名选择组件字段。")
            key = field[3]
            info = next((item for item in spec["fields"] if item["name"] == key), None)
            if info is None:
                raise ValueError(f"{spec['name']} 没有登记 {key}。")
            component_fields.check_value(value, info)
            checked.add((str(field[1]["Entity"]), spec["name"]))
            parent = selected_node(tree, field[:3], shared)
            present = any(k.value == key for k, _ in parent.value)
            if not reverse and "before_absent" in op and op["before_absent"] != (not present):
                raise ValueError(f"{key} 的保存状态已改变，请重新读取场景。")
            if proposing and info.get("optional") and not present:
                op["before_absent"] = True
            if op.get("before_absent") and not reverse:
                if not info.get("optional") or not same_value(component_fields.defaults(spec["name"])[key], expected):
                    raise ValueError(f"{key} 的默认值或保存状态已改变，请重新读取场景。")
                text = insert_optional(text, parent, key, value)
                continue

        script_value = scene_field and field[2:4] == ["ScriptComponent", "ScriptFields"] and field[-1] == "Value"
        if script_value:
            check_script_edit(tree, shared, op, value, proposing)
        node = selected_node(tree, field, shared)
        current = yaml.constructor.construct_object(node, deep=True)
        if not same_value(current, expected):
            raise ValueError(f"{path.name} 的 {field} 已改变，当前值为 {current!r}。")
        if reverse and spec and op.get("before_absent"):
            text = remove_optional(text, parent, key)
        else:
            if isinstance(value, list) and not (spec or script_value):
                raise ValueError("请选择一个文字、数字、开关或反射向量字段。")
            text = replace_value(text, node, current, value, shared)

    # 半径和总长允许一起改，所以每个组件按这批修改最终得到的形状检查。
    document = json.loads(text) if path.suffix == ".json" else yaml.load(text)
    for entity, name in checked:
        values = component_fields.saved_fields(component_fields.find_entity(document, entity), name)
        component_fields.validate(specs[name], values)
    return text.encode("utf8")


def prepare(project, operations, reverse=False, proposing=False):
    if not operations:
        raise ValueError("这次修改没有字段。")
    files = defaultdict(list)
    for op in reversed(operations) if reverse else operations:
        files[project_file(project,op["asset"])].append(op)
    result = {}
    for path,edits in files.items():
        source = path.read_bytes()
        result[path] = (source,rewrite(path,source,edits,reverse,proposing))
    return result


def change_folder(project, change):
    root = project / "captures/changes"
    folder = (root / change).resolve()
    if folder.parent != root or not (folder / "change.json").is_file():
        raise ValueError("这个项目中找不到这次修改。")
    return folder


def propose(project, intent, operations):
    if not intent.strip():
        raise ValueError("请填写修改意图。")
    operations = [dict(op) for op in operations]
    revisions = prepare(project,operations,proposing=True)
    key = uuid4().hex
    folder = project / "captures/changes" / key
    folder.mkdir(parents=True)
    record = {"id":key,"intent":intent,"state":"proposed","operations":operations,"base":{
        p.relative_to(project).as_posix():hashlib.sha256(old).hexdigest() for p,(old,_) in revisions.items()}}
    write_json(folder/"change.json",record)
    return record


def replace_bytes(path, previous, data):
    with NamedTemporaryFile(dir=path.parent,delete=False) as out:
        temporary = Path(out.name)
        out.write(data)
    try:
        if path.read_bytes() != previous:
            raise ValueError(f"{path.name} 在保存过程中被另一个作者修改。")
        os.replace(temporary,path)
    finally:
        temporary.unlink(missing_ok=True)


def recover(project, root):
    journal = root/"pending.json"
    if not journal.exists():
        return
    job = read_json(journal)
    folder = change_folder(project,job["id"])
    # 记录已提交、随后程序才退出的情况只需清理日志。否则逐个恢复已写入的文件。
    if read_json(folder/"change.json")["state"] != job["next"]:
        for i,name in reversed(list(enumerate(job["assets"][:job["attempted"]]))):
            path = project_file(project,name)
            before = (folder/f"{i}.before").read_bytes()
            after = (folder/f"{i}.after").read_bytes()
            current = path.read_bytes()
            if current == after:
                replace_bytes(path,after,before)
            elif current != before:
                raise ValueError(f"{name} 在未结束的保存中又被修改。已保留现场，请对照 {folder} 中的两份文件。")
    journal.unlink()


def apply(project, change, undo=False):
    root = project / "captures/changes"
    folder = change_folder(project,change)
    with writer_lock(root):
        recover(project,root)
        record = read_json(folder/"change.json")
        next_state = "undone" if undo else "applied"
        if record["state"] == next_state:
            return record
        if undo and record["state"] != "applied":
            raise ValueError("这次修改还没有应用。")
        revisions = prepare(project,record["operations"],undo)
        if undo:
            # 没有其他手工改动时，连同原先的引号、换行和数字写法一起恢复。
            paths = dict.fromkeys(project_file(project, op["asset"]) for op in record["operations"])
            for i, path in enumerate(paths):
                old, _ = revisions[path]
                if old == (folder / f"{i}.after").read_bytes():
                    revisions[path] = (old, (folder / f"{i}.before").read_bytes())
        names = []
        for i,(path,(old,new)) in enumerate(revisions.items()):
            names.append(path.relative_to(project).as_posix())
            (folder/f"{i}.before").write_bytes(old)
            (folder/f"{i}.after").write_bytes(new)
        pending = {"id":change,"next":next_state,"assets":names,"attempted":0}
        write_json(root/"pending.json",pending)
        try:
            for i,(path,(old,new)) in enumerate(revisions.items()):
                if path.read_bytes() != old:
                    raise ValueError(f"{path.name} 在本次保存期间被另一个作者修改。")
                pending["attempted"] = i+1
                write_json(root/"pending.json",pending)
                replace_bytes(path,old,new)
            record["state"] = next_state
            write_json(folder/"change.json",record)
        finally:
            recover(project,root)
    return record


if __name__ == "__main__":
    cli = argparse.ArgumentParser(description=__doc__)
    cmd = cli.add_subparsers(dest="operation",required=True)
    apply_cli = cmd.add_parser("apply")
    apply_cli.add_argument("--project",type=Path,default=PROJECT)
    apply_cli.add_argument("--change",required=True)
    apply_cli.add_argument("--undo",action="store_true")
    options = cli.parse_args()
    print(json.dumps(apply(options.project.resolve(),options.change,options.undo),ensure_ascii=False))
