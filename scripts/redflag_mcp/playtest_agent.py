"""从当前一帧选择下一步操作。每次让模型重新看到动作后的世界与开发者留下的意见。"""
import base64
import json
import math
from pathlib import Path
import time
from urllib.request import Request, urlopen
from urllib.error import URLError

from playtest import append_event, await_receipt, current, frames, issue, read_json, writer_lock
from playtest_metrics import load_profile


def last_rows(path, count):
    if not path.is_file():
        return []
    with path.open("rb") as stream:
        end = stream.seek(0, 2)
        size = 4096
        while end:
            start = max(0, end-size)
            stream.seek(start)
            rows = stream.read(end-start).split(b"\n")[:-1]
            if start and rows:
                rows = rows[1:]
            if not start or len(rows) >= count:
                return [json.loads(row) for row in rows[-count:]]
            size *= 2
    return []


def position(frame, actor):
    return next((a.get("position") for a in frame["actors"] if str(a["id"]) == actor), None)


def ask_model(endpoint, request, active, route="decisions"):
    url = endpoint.rstrip("/") + "/v1/" + route
    data = json.dumps(request,ensure_ascii=False).encode("utf8")
    with urlopen(Request(url,data,{"Content-Type":"application/json"}), timeout=8) as response:
        job = json.load(response)["id"]
    url += "/" + job
    try:
        deadline = time.monotonic() + 35
        while active() and time.monotonic() < deadline:
            with urlopen(url, timeout=5) as response:
                answer = json.load(response)
            if answer["status"] == "completed":
                return answer["choice" if route == "decisions" else "text"]
            if answer["status"] != "pending":
                raise RuntimeError(answer.get("error","模型无法选择操作"))
            time.sleep(.06)
        if active():
            raise TimeoutError("模型在本轮试玩中没有及时回答。")
        return None
    finally:
        try:
            with urlopen(Request(url,method="DELETE"),timeout=5):
                pass
        except URLError:
            pass


def explore(project: Path, folder: Path, profile: str, rounds: int, endpoint="http://127.0.0.1:8716", see=True):
    with writer_lock(folder, ".planner", wait=False):
        return play_rounds(project, folder, profile, rounds, endpoint, see)


def play_rounds(project, folder, profile, rounds, endpoint, see):
    if not 1 <= rounds <= 100:
        raise ValueError("每次可试玩 1 到 100 步。")
    controls = load_profile(project,profile)
    actor = str(controls["actor"])
    actions = controls["actions"]
    if any(not 0 < a["seconds"] <= 8 or not a["key"] for a in actions.values()):
        raise ValueError("每步须提供按键和 0 到 8 秒之间的持续时间。")
    start = current(folder)
    if not start["frame"] or start["control"]["owner"] != "agent":
        raise ValueError("等待游戏首帧，并在 Forge 中将操作权交给 AI。")
    generation = start["control"]["generation"]
    def active():
        control = read_json(folder / "handoff.json")
        status = folder / "status.json"
        return (control["generation"] == generation and control["owner"] == "agent" and
                (not status.exists() or read_json(status)["state"] not in ("exited","ended","stopped","crashed")))

    steps = []
    offset = 0
    history = []
    last_action = last_rows(folder/"decisions.jsonl",1)
    after_seq = last_action[0]["to"] if last_action else 0
    while len(steps) < rounds and active():
        frame = current(folder)["frame"]
        while True:
            batch = frames(folder,offset,600)
            offset = batch["offset"]
            for item in batch["frames"]:
                history.extend(dict(event,seq=item["seq"]) for event in item["events"])
            if len(batch["frames"]) < 600:
                break
        if any(e["kind"] in controls.get("finish_events",()) for e in history):
            break
        history = history[-16:]
        character = next((a for a in frame["actors"] if str(a["id"]) == actor), {})
        nearby = sorted((a for a in frame["actors"] if a.get("position") and str(a["id"]) != actor),
                        key=lambda a: math.dist(a["position"],character["position"]))[:12] if character.get("position") else []
        nearby = [dict(a,distance_m=round(math.dist(a["position"],character["position"]),2)) for a in nearby]
        recent = last_rows(folder/"decisions.jsonl",4)
        facts = {"角色":character, "周围":nearby, "最近事件":history,
                 "最近操作":recent,"开发者意见":last_rows(folder/"feedback.jsonl",3)}
        offered = dict(actions)
        # 同一个移动键已经连续两次没有产生位移，让模型本轮尝试别的操作。下一轮仍能重新选择它。
        if len(recent) >= 2 and len(actions) > 1:
            move = recent[-1]["choice"]
            if move in controls.get("checks",{}).get("movement",{}) and all(
                    s["choice"] == move and s["distance_m"] == 0 for s in recent[-2:]):
                del offered[move]
        play_request = folder/"play-goal.json"
        goal = read_json(play_request)["text"] if play_request.exists() else start["session"]["intent"]
        latest = history[-1]["text"] if history else "尚无事件"
        ask = {"prompt":f"目标：{goal}\n此刻的情况：{latest}。要达到目标，接下来使用哪个操作？",
               "instructions":"你在实际操作游戏。根据现在位置和最新游戏事件，选一个还需要执行的操作。只考虑当前局的操作，修改游戏参数由编辑器负责。",
               "observations":[{"id":key,"description":json.dumps(value,ensure_ascii=False)} for key,value in facts.items()],
               "candidates":[{"id":key,"description":value["description"]} for key,value in offered.items()]}
        photo = None
        if see:
            # 上一步刚走到阀门旁时，最近一张截图可能还拍着入口。只把操作结束后的图当作现况。
            deadline = time.monotonic()+2.5
            media = None
            while active() and time.monotonic() < deadline:
                media = next((r for r in reversed(last_rows(folder/"media.jsonl",40))
                              if r.get("image") and r["seq"] >= after_seq),None)
                if media:
                    break
                time.sleep(.08)
            if media:
                path = (folder / media["image"]).resolve()
                if path.is_relative_to(folder) and path.is_file():
                    photo = media["seq"]
                    ask["images"] = [{"id":"game","mime":"image/jpeg","data":base64.b64encode(path.read_bytes()).decode("ascii")}]

        choice = ask_model(endpoint,ask,active)
        if not choice or not active():
            break
        append_event(folder/"model-inputs.jsonl",{"seq":frame["seq"],"prompt":ask["prompt"],"observations":ask["observations"],
                                                 "allowed":list(offered),"choice":choice,"image_seq":photo})
        action = actions[choice]
        start_frame = current(folder)["frame"]
        pressed = issue(folder,generation,"hold",key=action["key"],lease_ms=int(1000 * action["seconds"]) + 2000)
        try:
            accepted = await_receipt(folder,pressed["id"])
            if not accepted["ok"]:
                raise RuntimeError(accepted["reason"])
            # 按游戏时钟推进。同样的 W 在低帧率下可能只经过两帧，实际距离由角色状态给出。
            clock = current(folder)["frame"]["seconds"]
            deadline = time.monotonic() + action["seconds"] + 12.
            while active() and current(folder)["frame"]["seconds"] - clock < action["seconds"]:
                if time.monotonic() > deadline:
                    raise TimeoutError("游戏的帧记录停止了。")
                time.sleep(.03)
        finally:
            if active():
                released = issue(folder,generation,"release",key=action["key"])
                await_receipt(folder,released["id"])
        after = current(folder)["frame"]
        a,b = position(start_frame,actor), position(after,actor)
        step = {"choice":choice, "from":start_frame["seq"], "to":after["seq"], "image_seq":photo, "actor":actor,
                "before":a, "after":b, "distance_m":math.dist(a,b) if a and b else None, "input":pressed["id"]}
        append_event(folder/"decisions.jsonl",step)
        steps.append(step)
        after_seq = after["seq"]
    return {"steps":steps, "control":read_json(folder/"handoff.json")}
