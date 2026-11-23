"""在发布版画面上发问和追问，保留模型台词、设备播放记录和相应时刻的截图。"""
import json
import math
import re
import time
import wave

import playtest


def converse(state, command, say, screenshot, folder, companion):
    def actor(entity):
        return next(a for a in state()["frame"]["actors"] if str(a["id"]) == entity)

    turns = []
    offset = 0
    for index, (message, expected) in enumerate([
        ("你好，我叫青桐。你叫什么名字？", companion),
        ("我刚告诉你我叫什么名字？", "青桐"),
        ("你对我们现在待的这个维修站有什么印象？", None),
        ("我考试没考好，有点失落。你会怎么安慰我？", None),
    ], start=1):
        if index > 1:
            command("tap", "T")
            screenshot(f"input-{index}", 3)
        before = actor("1102").get("action")
        say(message)

        # 一个角色可以在玩家发问前主动报点。按玩家这句话定位帧，再找属于这次回答的语音。
        spoken = {}
        asked_at = None
        captured = False
        pending = False
        choices = []
        deadline = time.monotonic() + 90
        while time.monotonic() < deadline:
            batch = playtest.frames(folder, offset, 100)
            offset = batch["offset"]
            for frame in batch["frames"]:
                for event in frame["events"]:
                    if str(event["entity"]) != "1102":
                        continue
                    if event["kind"] == "玩家" and event["text"] == message:
                        asked_at = frame["seconds"]
                    if asked_at is not None and event["kind"] in ("选择", "模型选择", "子树提问"):
                        choices.append(event)
                    if asked_at is not None and event["kind"] == "对话" and not spoken:
                        spoken = {"question": message, "reply": event["text"], "frame": frame["seq"]}
                        spoken["answer_seconds"] = round(frame["seconds"] - asked_at, 2)
            media = folder / "media.jsonl"
            if spoken and media.is_file():
                rows = [json.loads(line) for line in media.read_bytes().splitlines(keepends=True)
                        if line.endswith(b"\n")]
                # 字幕和播放器都在游戏线程更新。帧编号比回答早一帧时，声音已经在绘制这一帧时提交。
                request = next((r for r in rows if r["entity"] == "1102" and r["event"] == "requested"
                                and r["seq"] >= spoken["frame"] - 1), None)
                playback = {r["event"]: r for r in rows if request and r["id"] == request["id"]}
                if request and "first_voice" not in playback and not pending:
                    screenshot(f"pending-{index}", 0)
                    pending = True
                if "first_voice" in playback and not captured:
                    assert "finished" not in playback, "错过了声音播放中的实机画面"
                    screenshot(f"reply-{index}", 0)
                    captured = True
                if "finished" in playback:
                    assert captured and playback["finished"]["device_samples"] > 0, playback
                    spoken["audio"] = playback["finished"]
                    first = playback["first_voice"]["seconds"]
                    spoken["voice_seconds"] = round(first - request["seconds"], 2)
                    spoken["question_to_voice_seconds"] = round(first - asked_at, 2)
                    with wave.open(str(folder / spoken["audio"]["file"])) as audio:
                        assert audio.getnframes() == spoken["audio"]["device_samples"], spoken["audio"]
                    # 音频结束后字幕会淡出。留半秒再保存空场景，供人对照同一处画面。
                    time.sleep(.5)
                    screenshot(f"after-{index}", 2)
                    spoken["expected_name"] = expected
                    spoken["name_correct"] = expected in spoken["reply"] if expected else None
                    spoken["action_before"] = before
                    spoken["action_after"] = actor("1102").get("action")
                    spoken["action_choices"] = choices
                    turns.append(spoken)
                    break
            time.sleep(.035)
        else:
            screenshot(f"timeout-{index}", 0)
            raise TimeoutError(f"第 {index} 轮未走完发问、答话、设备播放和结束：{spoken}")

    def player_position():
        return actor("1101")["position"]

    start = player_position()
    command("hold", "A", lease_ms=2000)
    try:
        deadline = time.monotonic() + 2
        while time.monotonic() < deadline and math.dist(start, player_position()) < .1:
            time.sleep(.04)
    finally:
        command("release", "A")
    end = player_position()
    assert math.dist(start, end) >= .1, (start, end)
    return {"turns": turns, "movement_after_last_reply_m": math.dist(start, end)}


def check_trace(path, turns):
    rows = [json.loads(line) for line in path.read_bytes().splitlines()]
    ids = {(r["generation"], r["entity"], r["id"]) for r in rows}
    assert len(ids) == len(rows), f"同一请求重复写入 {path}"
    questions = [r for r in rows if r["entity"] == "1102"]
    assert len(questions) == 2 * len(turns), f"请在 {path} 查看缺少或多出的请求"

    for turn, intent, reply in zip(turns, questions[::2], questions[1::2]):
        assert intent["kind"] == "对话意图" and reply["kind"] == "角色答话", (intent["id"], reply["id"])
        assert intent["status"] == reply["status"] == "已回答", (intent["outcome"], reply["outcome"])
        assert intent["choice"] in ("chat", "recall", "scene", "create"), (turn["question"], intent["choice"])
        assert intent["model_output"].strip() and intent["outcome"] and reply["outcome"], path
        assert intent["seq"] <= reply["seq"] == turn["frame"], (intent["id"], reply["id"], turn["frame"])
        for request in (intent, reply):
            message = json.loads(request["model_input"])["messages"][-1]
            assert message["role"] == "user" and turn["question"] in message["content"], (turn["question"], request["id"])
        assert reply["model_output"] == turn["reply"], (reply["id"], turn["frame"])


def check_replies(turns):
    sentences = set()
    for turn in turns:
        reply = turn["reply"].strip()
        assert reply and turn["name_correct"] is not False, turn
        assert turn["action_before"] == turn["action_after"] and not turn["action_choices"], turn
        # 四道题分别问身份、姓名、现场和考试。没有任何一题提供玩家衣着，也不应整句照搬上题。
        assert "工装" not in reply, turn
        current = {s.strip() for s in re.findall(r"[^，,。！？!?\n]+[。！？!?]?", reply) if len(s.strip()) >= 8}
        assert not sentences.intersection(current), turn
        sentences.update(current)
