"""按一次创作意图试玩、提出字段修改，再从同一场景开局比较。每次应用和撤销沿用 Forge 的修改记录。"""
import json
import math
import time
from urllib.request import urlopen

from ruamel.yaml import YAML

from asset_changes import apply, project_file, propose, selected
from playtest import append_event, begin, current, await_receipt, issue, read_json, run_folder, write_json
from playtest_agent import ask_model, explore, last_rows
from playtest_metrics import assess, load_profile


def editable_fields(project, profile):
    fields = {}
    for key, spec in profile.get("editable",{}).items():
        asset = project_file(project,spec["asset"])
        yaml = YAML(typ="safe")
        node = selected(yaml.compose(asset.read_text("utf-8-sig")),spec["field"])
        fields[key] = dict(spec, current=yaml.constructor.construct_object(node))
    return fields


def propose_revision(project, folder, profile_name, instruction, endpoint):
    profile = load_profile(project,profile_name)
    evidence = assess(folder,profile)
    fields = editable_fields(project,profile)
    if not fields:
        raise ValueError("项目尚未为这个操作表指定可修改的字段。")
    evidence_text = json.dumps({"findings":evidence["findings"][:12],"metres":evidence["measured_moves_m"],
                                "feedback":evidence["feedback"][-6:]},ensure_ascii=False)
    schema = {key:{k:v for k,v in spec.items() if k in ("current","range","choices","unit")}
              for key,spec in fields.items()}
    kinds = {bool:"boolean", int:"integer", float:"number", str:"string"}
    request = {"instructions":"你是游戏编辑器的参数编辑助手。回答只写需要修改的字段名及其新值，组成一个扁平的 JSON 对象。",
               "reply_fields":{key:kinds[type(spec["current"])] for key,spec in fields.items()},
               "prompt":f"本次操作要求：{instruction}\n测得的结果：{evidence_text}\n允许调整的字段和它们当前的值：{json.dumps(schema,ensure_ascii=False)}\n"
                        f"请写出修改后的值。只允许以下键：{', '.join(fields)}。不重复题目，无法确定时返回 {{}}。"}
    for attempt in range(2):
        text = ask_model(endpoint,request,lambda: True,"replies").strip()
        if text.startswith("```") and text.endswith("```"):
            text = "\n".join(text.splitlines()[1:-1])
        try:
            choices = json.loads(text)
            if not isinstance(choices,dict) or not choices.keys() <= fields.keys():
                raise ValueError("回答中有项目没有开放的字段。")
            break
        except ValueError as error:
            append_event(folder/"revision-errors.jsonl",{"reply":text,"error":str(error)})
            if attempt:
                raise
            request["prompt"] += f"\n上次回答未能读取。请仅返回含这些键的新值，键名逐字照写：{', '.join(fields)}。"
    edits = []
    for key,value in choices.items():
        if key not in fields:
            raise ValueError(f"本项目未开放字段 {key}。")
        spec = fields[key]
        old = spec["current"]
        if isinstance(old,float) and type(value) is int:
            value = float(value)
        if type(old) is not type(value):
            append_event(folder/"revision-errors.jsonl",{"reply":text,"error":"type","field":key})
            raise ValueError(f"{key} 的值 {value!r} 与原字段类型不符。")
        if "range" in spec and (not math.isfinite(value) or not spec["range"][0] <= value <= spec["range"][1]):
            raise ValueError(f"{key} 超出项目给定的范围。")
        if "choices" in spec and value not in spec["choices"]:
            raise ValueError(f"{key} 使用了项目中没有的选项。")
        if value != old:
            edits.append({"asset":spec["asset"],"field":spec["field"],"before":old,"after":value})
    change = propose(project,instruction,edits) if edits else None
    result = {"source_session":folder.name,"model_fields":choices,"change":change["id"] if change else None}
    write_json(folder/"revision.json",result)
    return result


def play(project, scene, intent, profile, rounds, endpoint, executable=None, play_goal=None):
    session = begin(project,scene,intent,executable=executable)
    folder = run_folder(project,session["session"])
    if play_goal:
        write_json(folder/"play-goal.json",{"text":play_goal})
    deadline = time.monotonic()+90
    # 等首张游戏画面才交给视觉模型。启动崩溃会在 process.json 指向的游戏日志里留下原因。
    while time.monotonic() < deadline:
        info = current(folder)
        if info["status"]["state"] in ("exited","stopped"):
            raise RuntimeError(f"游戏在首张画面前退出：{folder}")
        if info["frame"] and any((folder/"images").glob("*.jpg")):
            break
        time.sleep(.15)
    else:
        raise TimeoutError(f"等待游戏初始画面超时：{folder}")
    owner = info["control"]
    try:
        played = explore(project,folder,profile,rounds,endpoint)
        if played["steps"]:
            final_seq = played["steps"][-1]["to"]
            until = time.monotonic()+3.
            while time.monotonic() < until and current(folder)["control"] == owner:
                if any(r.get("image") and r["seq"] >= final_seq for r in last_rows(folder/"media.jsonl",30)):
                    break
                time.sleep(.1)
    finally:
        # 开局时的许可只在同一代有效。人在 Forge 接管后，原模型不能结束他正在玩的一局。
        if current(folder)["control"] == owner and owner["owner"] == "agent":
            command = issue(folder,owner["generation"],"end")
            receipt = await_receipt(folder,command["id"])
            if not receipt["ok"]:
                raise RuntimeError(f"游戏未接受结束请求：{receipt}")
    result = assess(folder,load_profile(project,profile))
    write_json(folder/"assessment.json",result)
    return folder,result


def iterate(project,scene,intent,profile,rounds=4,revisions=1,endpoint="http://127.0.0.1:8716",executable=None):
    if not 1 <= rounds <= 100 or not 0 <= revisions <= 8:
        raise ValueError("每局可走 1 到 100 步，每轮最多修订 8 次。")
    with urlopen(endpoint.rstrip("/")+"/v1/health",timeout=5) as response:
        provider = json.load(response)["model"]
    attempts = []
    changes = []
    goal = ask_model(endpoint,{"instructions":"概括玩家在游戏内要达成的结果。只写一句话，忽略开发者修改代码、调字号、修复参数的指示。不添加新条件，也不用介绍按键和执行步骤。",
                               "prompt":intent},lambda: True,"replies")
    for index in range(revisions+1):
        folder, assessment = play(project,scene,intent,profile,rounds,endpoint,executable,goal)
        attempts.append(assessment)
        report = {"model":provider,"play_goal":goal,"sessions":[a["session"] for a in attempts],"changes":changes,
                  "attempts":attempts}
        write_json(folder/"iteration.json",report)
        if current(folder)["control"] != {"owner":"agent","generation":1}:
            report["stopped"]="handoff"
            break
        if not assessment["findings"] or index == revisions:
            break
        suggestion = propose_revision(project,folder,profile,intent,endpoint)
        if not suggestion["change"]:
            report["stopped"]="no_edit"
            break
        change = apply(project,suggestion["change"])
        changes.append(change["id"])
        write_json(folder/"iteration.json",report)
    write_json(folder/"iteration.json",report)
    return report
