"""按 From 和 To 修改动作表中的一条过渡；相邻状态与开发者的注释保留原文。"""
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import subprocess
from uuid import uuid4

from ruamel.yaml import YAML
from asset_changes import recover
from npc_assets import TOOL, ToolError, fields, read, replace_file, replace_value, same_value
from playtest import write_json, writer_lock
from playtest_metrics import assess_transition
from project import PROJECT


def asset(name, suffix=".animator.yaml"):
    assets = (PROJECT / "assets").resolve()
    path = (assets / name).resolve()
    if not path.is_relative_to(assets) or not path.name.endswith(suffix) or not path.is_file():
        raise ToolError(f"请填写 assets 内已有的 {suffix} 文件路径。")
    return path


def inspect(path):
    validate(path)
    return read_settings(path)


def read_settings(path):
    doc = YAML(typ="safe").load(read(path))
    return {"states": [row["ID"] for row in doc["States"]],
            "transitions": [{"From": row["From"], "To": row["To"], "Seconds": row["Seconds"],
                             "Control": row.get("Control", [0, 1])} for row in doc.get("Transitions", [])]}


def owning_project(path):
    return next((p.parent for p in path.parents if p.name == "assets"), path.parent)


def run_native(path, operation, *args):
    # 动作表里的 assets/... 引用从当前游戏项目查找。相对引用由引擎从动作表所在目录查找。
    path = path.resolve()
    result = subprocess.run([str(TOOL), operation, str(path), *args], cwd=owning_project(path), capture_output=True,
                            encoding="utf-8", timeout=15, creationflags=subprocess.CREATE_NO_WINDOW)
    if result.returncode:
        raise ToolError(result.stderr.strip() or "引擎无法读取这份动作资产。")
    return result.stdout


def validate(path, operation="validate-animator"):
    run_native(path, operation)


def save(path: Path, source: str, target: str, original, value):
    text = read(path)
    yaml = YAML(typ="safe")
    document = yaml.load(text)
    rows = document.get("Transitions") or []
    index = next((i for i, row in enumerate(rows) if (row["From"], row["To"]) == (source, target)), None)
    current = None if index is None else {"Seconds": rows[index]["Seconds"], "Control": rows[index].get("Control", [0, 1])}
    if same_value(current, value):
        return False
    if not same_value(current, original):
        raise ToolError("这条过渡已被其他作者修改，请重新打开动作表。")

    seq = fields(yaml.compose(text)).get("Transitions")
    newline = "\r\n" if "\r\n" in text else "\n"
    if index is None:
        entry = json.dumps({"From":source, "To":target, "Seconds":value["Seconds"], "Control":value["Control"]},
                           ensure_ascii=False, allow_nan=False)
        if seq is None:
            edited = text + ("" if text.endswith("\n") else newline) + "Transitions:" + newline + "  - " + entry + newline
        elif not isinstance(seq.value, list):
            edited = text[:seq.start_mark.index] + " [" + entry + "]" + text[seq.end_mark.index:]
        elif seq.flow_style:
            at = seq.end_mark.index - 1
            edited = text[:at] + (", " if rows else "") + entry + text[at:]
        else:
            at = seq.start_mark.index
            edited = text[:at] + "- " + entry + newline + " " * seq.start_mark.column + text[at:]
    elif value is not None:
        row = seq.value[index]
        keys = fields(row)
        edits = [(keys["Seconds"], value["Seconds"])]
        if "Control" in keys:
            if len(value["Control"]) != 2:
                raise ToolError("Control 需要两个控制点纵坐标。")
            edits.extend(zip(keys["Control"].value, value["Control"]))
        edited = text
        for node, val in sorted(edits, key=lambda change: change[0].start_mark.index, reverse=True):
            edited = replace_value(edited, node, val)
        if "Control" not in keys:
            row = fields(yaml.compose(edited))["Transitions"].value[index]
            args = "Control: " + json.dumps(value["Control"], allow_nan=False)
            at = row.start_mark.index + (1 if row.flow_style else 0)
            sep = ", " if row.flow_style else newline + " " * row.start_mark.column
            edited = edited[:at] + args + sep + edited[at:]
    else:
        row = seq.value[index]
        start, end = row.start_mark.index, row.end_mark.index
        if seq.flow_style:
            if index:
                start = text.rindex(",", seq.value[index-1].end_mark.index, start)
            elif len(rows) > 1:
                end = text.index(",", end, seq.value[1].start_mark.index) + 1
            edited = text[:start] + text[end:]
        else:
            # 保留相邻条目之间的注释。块式映射的 end_mark 可能跨过下一条注释。
            markers = list(re.finditer(r"(?m)^" + " " * seq.start_mark.column + r"-(?=\s)", text[:start+1]))
            start = markers[-1].end() - 1
            if not row.flow_style:
                end = max(v.end_mark.index if not isinstance(v.value, list) or v.flow_style
                          else v.value[-1].end_mark.index for _, v in row.value)
            edited = text[:start] + ("[]" if len(rows) == 1 else "") + text[end:]
    replace_file(path, text, edited, validate)
    return True


# 应用时才回读资产。两次拖动滑块之间的草稿不进入项目记录。

def sample(path, source, target):
    return YAML(typ="safe").load(run_native(path, "sample-transition", source, target))


def apply(path, source, target, original, value):
    path = path.resolve()
    project = owning_project(path)
    folder = project / "captures/changes/animation-transitions"
    folder.mkdir(parents=True, exist_ok=True)
    with writer_lock(folder.parent):
        recover(project, folder.parent)
        if not save(path, source, target, original, value):
            validate(path)
            return {}
        record = {"asset": path.relative_to(project).as_posix(), "from": source, "to": target,
                  "before": original, "after": value, "applied": datetime.now(timezone.utc).isoformat()}
        result = {}
        try:
            record["measurement"] = sample(path, source, target)
            record["assessment"] = assess_transition(record["measurement"], value)
        except Exception as error:
            # 资产已保存。保留错误与实测值，Forge 仍应把左栏更新成已写入的版本。
            record["assessment"] = {"verdict": "findings", "findings": [{"kind": "transition_sample_error",
                                                                        "message": str(error)}]}
        findings = record["assessment"]["findings"]
        if findings:
            result["warning"] = "已应用，过渡复查未通过。" + findings[0]["message"]

        report = folder / (uuid4().hex + ".json")
        try:
            write_json(report, record)
            result["record"] = str(report)
        except (OSError, ValueError) as error:
            result.setdefault("warning", "已应用，无法保存这次过渡的复查记录。" + str(error))
        return result
