"""用电子表格改一个角色的设定或行为树连线。命令与 MCP 共用同一套保存和校验。"""
import argparse
import csv
from pathlib import Path

import npc_assets


PROFILE = {"名称": ("Name", "name"), "角色说明": ("Instructions", "instructions"), "音色": ("Voice", "voice"),
           "与指定队友的约定": ("LeaderRelationship", "leader_relationship"),
           "决策模型": ("Model", "model"), "待机动画": ("IdleClip", "idle"), "行走动画": ("WalkClip", "walk"),
           "使用角色记忆": ("Remember", "remember"), "观察场景物体": ("ObserveWorld", "observe_world"),
           "观察距离（米）": ("Sight", "sight")}
INTENTS = {"动作说明": ("Description", "description"), "目标来源": ("Target", "target"), "物品": ("Item", "item")}


def table_rows(name: str, table: str):
    data = npc_assets.inspect(name)
    if table == "profile":
        yield ["角色文件", "字段", "原值", "改成"]
        for label, (_, key) in PROFILE.items():
            yield [name, label, data[key], data[key]]
    elif table == "intents":
        yield ["角色文件", "候选键", "字段", "原值", "改成"]
        for intent in data["routes"]:
            for label, (_, key) in INTENTS.items():
                yield [name, intent["candidate"], label, intent[key], intent[key]]
    else:
        yield ["角色文件", "起点编号", "候选键", "原终点编号", "改成"]
        for edge in data["graph"]["Edges"]:
            yield [name, edge["From"], edge.get("Key", ""), edge["To"], edge["To"]]


def dialect(path: Path):
    if path.suffix.lower() not in (".csv", ".tsv"):
        raise ValueError("表格文件请使用 .csv 或 .tsv 后缀。")
    return "excel" if path.suffix.lower() == ".csv" else "excel-tab"


def export(name: str, table: str, path: Path):
    format = dialect(path)
    with path.open("w", encoding="utf-8-sig", newline="") as f:
        csv.writer(f, dialect=format).writerows(table_rows(name, table))


def apply(name: str, table: str, path: Path) -> dict:
    rows = iter(table_rows(name, table))
    header = next(rows)
    current = {tuple(str(v) for v in row[:-2]): str(row[-1]) for row in rows}
    changes = {}
    with path.open(encoding="utf-8-sig", newline="") as f:
        sheet = csv.reader(f, dialect=dialect(path))
        if next(sheet, []) != header:
            raise ValueError("请保留导出表中的列名和顺序。")
        for row in sheet:
            if not row:
                continue
            key = tuple(row[:-2])
            if len(row) != len(header) or key not in current:
                raise ValueError(f"第 {sheet.line_num} 行不属于这个角色或这张表。")
            old, new = row[-2:]
            if new == old or new == current[key]:
                continue
            if current[key] != old or key in changes:
                raise ValueError(f"第 {sheet.line_num} 行有重复或原值已改变，请重新导出。")
            changes[key] = new

    if table == "profile":
        values = {}
        for key, value in changes.items():
            field = PROFILE[key[1]][0]
            if field in ("Remember", "ObserveWorld"):
                if value.lower() not in ("true", "false"):
                    raise ValueError(f"{key[1]} 请填写 True 或 False。")
                value = value.lower() == "true"
            if field == "Sight":
                value = float(value)
            values[field] = value
        return npc_assets.update(name, values)
    if table == "intents":
        return npc_assets.update_intents(name, {(k[1], INTENTS[k[2]][0]): v for k, v in changes.items()})
    return npc_assets.update_links(name, {(int(key[1]), key[2]): int(value) for key, value in changes.items()})


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("export", "apply"))
    parser.add_argument("npc", help="角色文件，如 watt.npc.yaml")
    parser.add_argument("table", choices=("profile", "links", "intents"))
    parser.add_argument("file", type=Path, help="UTF-8 表格 (.tsv 或 .csv)")
    args = parser.parse_args()
    try:
        if args.command == "export":
            export(args.npc, args.table, args.file)
            print("表格已导出。编辑「改成」一列，然后用 apply 保存。")
        else:
            result = apply(args.npc, args.table, args.file)
            print(f"已读取 {result['name']} 的角色设置和 {len(result['routes'])} 个候选的执行路径。")
    except (OSError, ValueError, npc_assets.ToolError) as e:
        parser.exit(1, f"{e}\n")
