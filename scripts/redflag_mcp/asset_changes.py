"""按字段审查、保存和撤销一次创作修改。每次只替换选中的值，保留作者在其他位置的修改。"""
from collections import defaultdict
import argparse
import hashlib
import json
import os
from pathlib import Path
from tempfile import NamedTemporaryFile
from uuid import uuid4

from ruamel.yaml import YAML
from ruamel.yaml.nodes import MappingNode, ScalarNode, SequenceNode

from playtest import read_json, write_json, writer_lock
from project import PROJECT


def project_file(project, name):
    path = (project / name).resolve()
    if not path.is_relative_to(project / "assets") or path.suffix.lower() not in (".redflag",".yaml",".yml",".json") or not path.is_file():
        raise ValueError(f"请选择当前项目中已有的场景或配置资产：{name}")
    return path


def selected(tree, route):
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
    if not isinstance(node,ScalarNode):
        raise ValueError("请选择一个文字、数字或开关字段。")
    return node


def rewrite(path, contents, edits):
    text = contents.decode("utf8")
    yaml = YAML(typ="safe")
    for field, expected, value in edits:
        node = selected(yaml.compose(text),field)
        if isinstance(value,(dict,list)) or isinstance(expected,(dict,list)):
            raise ValueError("修改内容应该是单个字段值。")
        current = yaml.constructor.construct_object(node)
        if type(current) is not type(expected) or current != expected:
            raise ValueError(f"{path.name} 的 {field} 已改变，当前值为 {current!r}。")
        start,end = node.start_mark.index,node.end_mark.index
        replacement = json.dumps(value,ensure_ascii=False,allow_nan=False)
        if text[start:end].endswith("\n"):
            replacement += "\r\n" if text[start:end].endswith("\r\n") else "\n"
        text = text[:start] + replacement + text[end:]
    if path.suffix == ".json":
        json.loads(text)
    else:
        yaml.load(text)
    return text.encode("utf8")


def prepare(project, operations, reverse=False):
    if not operations:
        raise ValueError("这次修改没有字段。")
    files = defaultdict(list)
    for op in reversed(operations) if reverse else operations:
        before,after = op["before"],op["after"]
        files[project_file(project,op["asset"])].append((op["field"],after if reverse else before,before if reverse else after))
    result = {}
    for path,edits in files.items():
        source = path.read_bytes()
        result[path] = (source,rewrite(path,source,edits))
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
    revisions = prepare(project,operations)
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
