"""沿游戏帧核对操作、位移和事件。结果给出帧号，以便回到当时的画面或声音。"""
from collections import Counter
import json
import math
import wave

from PIL import Image, ImageChops, ImageStat

from playtest import read_json


def rows(path):
    if path.exists():
        with path.open("rb") as stream:
            for line in stream:
                if line.endswith(b"\n"):
                    yield json.loads(line)


def load_profile(project, name):
    root = (project / "assets/playtests").resolve()
    path = (root / name).resolve()
    if not path.is_relative_to(root) or path.suffix != ".json":
        raise ValueError("请选择项目的试玩配置。")
    return read_json(path)


def percentiles(values):
    values.sort()
    return {k: values[math.ceil(len(values)*p)-1] if values else None
            for k,p in (("p50",.50),("p95",.95),("p99",.99))}


def media_evidence(folder, media, rules, finding):
    pictures, audio = [], {}
    previous = None
    for row in media:
        is_audio = row.get("kind") == "audio"
        name = row.get("file" if is_audio else "image")
        if not name:
            continue
        path = (folder/name).resolve()
        if not path.is_relative_to(folder.resolve()) or not path.is_file():
            finding("missing_recording",row["seq"],row["seq"],file=name)
            continue
        if is_audio:
            if row["id"] not in audio:
                with wave.open(str(path),"rb") as wav:
                    audio[row["id"]] = {"file":name,"samples":wav.getnframes(),"hz":wav.getframerate(),"events":[]}
            clip = audio[row["id"]]
            clip["events"].append({"event":row["event"],"seq":row["seq"],"samples":row["device_samples"]})
            if row["event"] in ("stopped","finished") and row["device_samples"] is not None:
                clip["played_s"] = row["device_samples"]/clip["hz"]
            continue
        with Image.open(path) as image:
            # 统一缩小后量亮度。举例来说，镜头需要看到地面时，可在项目里为纯黑输出设一个阈值。
            small = image.convert("L").resize((96,54),Image.Resampling.BOX)
        brightness = ImageStat.Stat(small).mean[0]/255
        change = ImageStat.Stat(ImageChops.difference(previous,small)).mean[0]/255 if previous is not None else None
        pictures.append({"seq":row["seq"],"seq_end":row.get("seq_end",row["seq"]),"file":name,
                         "mean_luma":round(brightness,4),"change":round(change,4) if change is not None else None})
        if "min_luma" in rules and brightness < rules["min_luma"]:
            finding("dark_output",row["seq"],row.get("seq_end",row["seq"]),brightness=brightness)
        previous = small
    return pictures,list(audio.values())


def assess(folder, profile):
    rules = profile.get("checks", {})
    actor = str(profile["actor"])
    timings, events, inputs, issues = [], [], [], []
    first = last = prev_time = previous = None
    decisions = list(rows(folder / "decisions.jsonl"))
    endpoints = {step[key] for step in decisions for key in ("from","to")}
    positions = {}
    observed = 0
    travelled = 0.

    def finding(kind, start, end, **values):
        issues.append(dict(kind=kind, start=start, end=end, **values))

    for frame in rows(folder / "frames.jsonl"):
        seq, now = frame["seq"], frame["seconds"]
        if first is None:
            first = seq
        if last is not None and (seq != last+1 or now < prev_time):
            finding("timeline_break", last, seq)
        last, prev_time = seq, now
        ms = frame["frame_ms"]
        if math.isfinite(ms) and ms >= 0:
            timings.append(ms)
        else:
            finding("invalid_frame_time", seq, seq)
        for collection, key in ((events,"events"), (inputs,"inputs")):
            collection.extend(dict(item, seq=seq, seconds=now) for item in frame[key])

        target = next((a for a in frame["actors"] if str(a["id"]) == actor), None)
        pos = target["position"] if target else None
        if pos is not None and not all(math.isfinite(v) for v in pos):
            finding("invalid_position", seq, seq, actor=actor)
            pos = None
        if pos is not None:
            observed += 1
            if seq in endpoints:
                positions[seq] = pos
            if previous is not None:
                delta = math.dist(previous,pos)
                travelled += delta
                if "max_step_m" in rules and delta > rules["max_step_m"]:
                    finding("position_jump", seq-1, seq, metres=delta)
        previous = pos

    # 模型实际按下的动作与松开后的角色坐标配对。作者指定哪些动作原本应该产生位移。
    measured, redundant = [], []
    for step in decisions:
        if step["choice"] not in rules.get("movement", {}):
            continue
        start, end = step["from"], step["to"]
        a,b = positions.get(start), positions.get(end)
        if a is None or b is None:
            finding("missing_actor", start, end, actor=actor)
            continue
        distance = math.dist(a,b)
        measured.append(distance)
        rule = rules["movement"][step["choice"]]
        if isinstance(rule,dict):
            # 人物到目的地后再按前进是多余操作。保留给作者审查，同时单独核对到达前的移动。
            arrived = any(e["kind"] == rule.get("until_event") and e["seq"] <= start and
                          str(e["entity"]) == actor for e in events)
            if arrived:
                if distance == 0:
                    redundant.append({"from":start,"to":end,"action":step["choice"]})
                continue
            minimum = rule["min_m"]
        else:
            minimum = rule
        if distance < minimum:
            finding("no_progress", start, end, action=step["choice"], metres=distance, minimum_m=minimum)
    if rules.get("movement") and not measured:
        finding("movement_not_observed", first, last)

    # 按事件的先后配对。第二次开始不能替第一次补上后来才出现的结束事件。
    for rule in rules.get("transitions", []):
        queue, count = [], 0
        for event in events:
            if rule.get("entity") and str(rule["entity"]) != str(event["entity"]):
                continue
            if event["kind"] == rule["from"]:
                queue.append(event)
            elif event["kind"] == rule["to"] and queue:
                before = queue.pop(0)
                elapsed = event["seconds"] - before["seconds"]
                count += 1
                if elapsed > rule["within_s"]:
                    finding("late_transition",before["seq"],event["seq"],seconds=elapsed,rule=rule)
        for before in queue:
            finding("unfinished_transition",before["seq"],last,rule=rule)
        if not count and not queue and rule.get("required", True):
            finding("transition_not_observed",first,last,rule=rule)

    media = list(rows(folder/"media.jsonl"))
    pictures, clips = media_evidence(folder,media,rules,finding)
    speech = [r for r in media if r.get("kind") == "audio"]
    channels = dict(state=observed, input=len(inputs), event=len(events), image=len(pictures), audio=len(clips), performance=len(timings))
    for channel in rules.get("required_channels", []):
        if not channels[channel]:
            finding("missing_channel",first,last,channel=channel)
    if first is None:
        finding("no_frames",None,None)
    metrics = percentiles(timings)
    if timings and "p95_ms" in rules and metrics["p95"] > rules["p95_ms"]:
        finding("slow_frames", first,last,p95_ms=metrics["p95"],budget_ms=rules["p95_ms"])
    status = read_json(folder/"status.json") if (folder/"status.json").is_file() else {}
    if status.get("state") == "exited" and status.get("exit_code") != 0:
        finding("game_exited",last,last,exit_code=status.get("exit_code"))
    rejected = []
    for receipt in sorted((folder/"receipts").glob("*.json")):
        ack = read_json(receipt)
        if not ack["ok"]:
            rejected.append(ack)
    return {"session":folder.name,"actor":actor,"from":first,"to":last,"status":status.get("state"),
            "verdict":"findings" if issues else ("observed" if not rules else "met_checks"),
            "channels":channels,"events":dict(Counter(e["kind"] for e in events)), "frame_ms":metrics,
            "audio_events":dict(Counter(r["event"] for r in speech)),
            "screens":pictures,"speech":clips,
            "travel_m":travelled,"measured_moves_m":measured,"rejected_inputs":rejected,
            "redundant_inputs":redundant,
            "feedback":list(rows(folder/"feedback.jsonl")),"findings":issues}
