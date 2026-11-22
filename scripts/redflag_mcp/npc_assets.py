"""编辑已保存的 NPC 资产。角色读取、连线校验和试跑共用引擎的 C++ 实现。"""
import json
import os
import subprocess
import struct
from pathlib import Path
from tempfile import NamedTemporaryFile
from threading import RLock

from mcp.server.mcpserver.exceptions import ToolError
from ruamel.yaml import YAML
from ruamel.yaml.nodes import MappingNode
from project import ENGINE, PROJECT


ROOT = ENGINE
ASSETS = PROJECT / "assets/ai"
TOOL = ROOT / "bin-int/Tools/Npc/NpcTool.exe"
_write_lock = RLock()


# 角色、行为树和运行中的工具

def asset(name: str, suffix: str, existing: bool = True) -> Path:
    path = ASSETS / name
    if not name.endswith(suffix) or path.name != name or path.resolve().parent != ASSETS or path.is_symlink():
        raise ToolError(f"请使用角色目录中的 {suffix} 文件名。")
    if existing and not path.is_file():
        raise ToolError(f"找不到 {name}。")
    return path


def list_roles():
    return sorted(p.name for p in ASSETS.glob("*.npc.yaml") if p.is_file() and not p.is_symlink())


def read(path: Path) -> str:
    return path.read_bytes().decode("utf-8")


def same_value(a, b):
    # 引擎把动画时间读成 float。0.18 秒存入 float 后在文本中可能写为 0.180000007。
    if isinstance(a, list) and isinstance(b, list):
        return len(a) == len(b) and all(same_value(x, y) for x, y in zip(a, b))
    if isinstance(a, dict) and isinstance(b, dict):
        return a.keys() == b.keys() and all(same_value(a[k], b[k]) for k in a)
    if isinstance(a, (int, float)) and isinstance(b, float):
        return struct.pack("f", a) == struct.pack("f", b)
    return a == b


def tree_file(role: Path) -> Path:
    profile = YAML(typ="safe").load(read(role))
    return asset(profile["Tree"], ".tree.yaml")


def animator_file(role: Path):
    name = YAML(typ="safe").load(read(role)).get("Animator")
    return asset(name, ".animator.yaml") if name else None


def run(role: Path) -> dict:
    if not TOOL.is_file():
        raise ToolError("请先从仓库根目录运行 scripts/redflag_mcp/build_npc_tool.cmd。")
    process = subprocess.run([str(TOOL), "inspect", str(role)], capture_output=True,
                             encoding="utf-8", timeout=15, creationflags=subprocess.CREATE_NO_WINDOW)
    if process.returncode:
        raise ToolError(process.stderr.strip() or "角色工具退出，未返回结果。")
    return YAML(typ="safe").load(process.stdout)


def inspect(name: str) -> dict:
    role = asset(name, ".npc.yaml")
    tree = tree_file(role)
    animator_file(role)
    result = run(role)
    profile = YAML(typ="safe").load(read(role))
    graph = YAML(typ="safe").load(read(tree))
    return {**result, "file": name, "instructions": profile["Instructions"], "graph": graph,
            "source": "saved NPC files and native BehaviorTree::Tick"}


# 单字段修改。保留作者手写的注释、缩进和其他行。

def fields(node: MappingNode) -> dict:
    return {key.value: value for key, value in node.value}


def replace_value(text: str, node, value) -> str:
    start, end = node.start_mark.index, node.end_mark.index
    encoded = json.dumps(value, ensure_ascii=False)
    # 多行说明的最后一个换行也属于 YAML 节点。换成引号字符串后留出下一行。
    if text[start:end].endswith("\n"):
        encoded += "\r\n" if text[start:end].endswith("\r\n") else "\n"
    return text[:start] + encoded + text[end:]


def change_intents(text, node, rows):
    current = YAML(typ="safe").load(text)["Intents"]
    if [r["Key"] for r in current] != [r["Key"] for r in rows]:
        return replace_value(text, node, rows)
    for saved, edited, item in reversed(list(zip(current, rows, node.value))):
        keys = fields(item)
        changed = {k: v for k, v in edited.items() if saved.get(k, "") != v}
        if any(k not in keys for k in changed) or ("Item" in saved and "Item" not in edited):
            text = replace_value(text, item, edited)
        else:
            for key in sorted(changed, key=lambda k: keys[k].start_mark.index, reverse=True):
                text = replace_value(text, keys[key], changed[key])
    return text


def change_fields(text: str, values: dict) -> str:
    for key, value in values.items():
        nodes = fields(YAML(typ="safe").compose(text))
        if key in nodes:
            if key == "Intents":
                text = change_intents(text, nodes[key], value)
            elif nodes[key].value != value:
                text = replace_value(text, nodes[key], value)
        else:
            newline = "\r\n" if "\r\n" in text else "\n"
            if not text.endswith("\n"):
                text += newline
            text += f"{key}: {json.dumps(value, ensure_ascii=False)}{newline}"
    return text


def update_intents(name: str, changes: dict) -> dict:
    with _write_lock:
        role = asset(name, ".npc.yaml")
        tree_file(role)
        before = read(role)
        original = YAML(typ="safe").load(before)["Intents"]
        rows = [dict(row) for row in original]
        for row in rows:
            for (key, field), value in changes.items():
                if row["Key"] == key:
                    row[field] = value
        return update(name, {"Intents": rows}, {"Intents": original}) if rows != original else inspect(name)


def replace_file(path: Path, before: str, after: str, validate=None):
    with NamedTemporaryFile(dir=path.parent, suffix=".tmp", delete=False) as f:
        temporary = Path(f.name)
    try:
        temporary.write_bytes(after.encode("utf-8"))
        if validate:
            validate(temporary)
        if read(path) != before:
            raise ToolError("编辑期间文件已被修改，请重新读取角色。")
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def update(name: str, changes: dict, expected: dict | None = None) -> dict:
    with _write_lock:
        role = asset(name, ".npc.yaml")
        tree_file(role)
        before = read(role)
        if expected:
            current = YAML(typ="safe").load(before)
            for key, value in expected.items():
                if key in current and not same_value(current[key], value) and not same_value(current[key], changes[key]):
                    raise ToolError(f"{key} 已被其他作者修改，请重新读取角色。")
        after = change_fields(before, changes)
        if after != before:
            replace_file(role, before, after, run)
        return inspect(name)


def copy(name: str, new_file: str, display_name: str) -> dict:
    with _write_lock:
        source = asset(name, ".npc.yaml")
        original_tree = tree_file(source)
        run(source)
        role = asset(new_file, ".npc.yaml", existing=False)
        tree = asset(new_file.removesuffix(".npc.yaml") + ".tree.yaml", ".tree.yaml", existing=False)
        files = [(tree, original_tree.read_bytes())]
        refs = {"Name": display_name, "Tree": tree.name}
        animator = animator_file(source)
        if animator:
            own = asset(new_file.removesuffix(".npc.yaml") + ".animator.yaml", ".animator.yaml", False)
            files.append((own, animator.read_bytes()))
            refs["Animator"] = own.name
        role_text = change_fields(read(source), refs)
        files.append((role, role_text.encode("utf-8")))
        created = []
        try:
            # 先建立独立的树，再发布引用它的角色。Windows 的 rename 遇到同名文件会拒绝覆盖。
            for path, data in files:
                with NamedTemporaryFile(dir=path.parent, suffix=".tmp", delete=False) as f:
                    temporary = Path(f.name)
                    f.write(data)
                try:
                    temporary.rename(path)
                except FileExistsError as error:
                    raise ToolError(f"已经存在 {path.name}，请选择新的文件名。") from error
                finally:
                    temporary.unlink(missing_ok=True)
                created.append(path)
            return inspect(role.name)
        except Exception:
            for path in reversed(created):
                path.unlink(missing_ok=True)
            raise


# 连接表中的一行由起点和候选键共同确定。

def validate_reference(role, field, candidate):
    profile = change_fields(read(role), {field: candidate.name})
    with NamedTemporaryFile(dir=role.parent, suffix=".tmp", delete=False) as f:
        temporary = Path(f.name)
        f.write(profile.encode("utf-8"))
    try:
        run(temporary)
    finally:
        temporary.unlink(missing_ok=True)


def edit_states(name: str, changes: dict, original: dict | None = None):
    with _write_lock:
        role = asset(name, ".npc.yaml")
        animator = animator_file(role)
        if not animator:
            raise ToolError("请先在角色创作中选择或创建动作表。")
        before = read(animator)
        document = YAML(typ="safe").load(before)
        stored = {r["ID"]: r for r in document["States"]}
        marks = fields(YAML(typ="safe").compose(before))["States"].value
        after = before
        for item in reversed(marks):
            keys = fields(item)
            state = keys["ID"].value
            if state not in changes:
                continue
            edits = changes[state]
            saved = stored[state]
            if original:
                for key, value in edits.items():
                    before_field = original[state][key]
                    current = saved.get(key, [] if key == "Events" else before_field)
                    if not same_value(current, before_field) and not same_value(current, value):
                        raise ToolError(f"{state}.{key} 已被其他作者修改。")
            if any(key not in keys for key in edits):
                after = replace_value(after, item, {**saved, **edits})
            else:
                for key in sorted(edits, key=lambda k: keys[k].start_mark.index, reverse=True):
                    after = replace_value(after, keys[key], edits[key])
        if changes.keys() - stored.keys():
            raise ToolError("动作表中找不到指定的状态 ID。")
        if after != before:
            replace_file(animator, before, after, lambda candidate: validate_reference(role, "Animator", candidate))
        return inspect(name)


def new_animator(name, clip):
    role = asset(name, ".npc.yaml")
    path = asset(name.removesuffix(".npc.yaml") + ".animator.yaml", ".animator.yaml", False)
    with _write_lock:
        with path.open("x", encoding="utf-8") as f:
            f.write(json.dumps({"States": [{"ID": "idle", "Clip": clip}]}, ensure_ascii=False, indent=2))
        try:
            return update(name, {"Animator": path.name})
        except Exception:
            path.unlink()
            raise

def update_links(name: str, changes: dict[tuple[int, str], int]) -> dict:
    with _write_lock:
        role = asset(name, ".npc.yaml")
        tree = tree_file(role)
        before = read(tree)
        edges = fields(YAML(typ="safe").compose(before))["Edges"]
        found = set()
        replacements = []
        for edge in edges.value:
            row = fields(edge)
            edge_key = row["Key"].value if "Key" in row else ""
            port = int(row["From"].value), edge_key
            if port in changes:
                found.add(port)
                if int(row["To"].value) != changes[port]:
                    replacements.append((row["To"], changes[port]))
        if found != changes.keys():
            raise ToolError("连接表里找不到这个起点和候选键。")

        after = before
        # 从后向前替换，只换终点的数字，前面各行在原文里的位置仍然正确。
        for node, target in reversed(replacements):
            after = replace_value(after, node, target)
        if after != before:
            replace_file(tree, before, after, lambda candidate: validate_reference(role, "Tree", candidate))
        return inspect(name)
