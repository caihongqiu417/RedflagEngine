"""单人试玩通过玩家能使用的通讯、移动和重新开始按钮推进。"""
import json
import math
import time

import playtest
from ruamel.yaml import YAML


def play_station(state, command, say, screenshot, folder, output, scene_path, test_defeat):
    stages = {}
    scene = YAML(typ="safe").load(scene_path.read_bytes())
    entities = {row["Entity"]: row for row in scene["Entities"]}
    player = next(e for e in entities.values() if e.get("ScriptComponent", {}).get("Class") == "Sandbox.StationPlayer")
    settings = {field["Name"]: field["Value"] for field in player["ScriptComponent"]["ScriptFields"]}
    companion = entities[settings["Watt"]]["TagComponent"]["Tag"]
    gate = entities[settings["Exit"]]["TransformComponent"]["Translation"]
    events = set()
    event_offset = 0

    def actors():
        return {actor["name"]: actor for actor in state()["frame"]["actors"]}

    def checkpoint(name):
        frame = state()["frame"]
        stages[name] = {"seq": frame["seq"], "actors": {a["name"]: a for a in frame["actors"]
                        if a["name"] in ("玩家", companion, "应急发电机")}}
        (output / "mission.json").write_text(json.dumps(stages, ensure_ascii=False, indent=2), encoding="utf8")
        print(name, frame["seq"], flush=True)

    def wait_for(name, predicate, timeout=28):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            world = actors()
            if predicate(world):
                checkpoint(name)
                return
            time.sleep(.06)
        checkpoint(name + "_timeout")
        screenshot("at-timeout")
        raise AssertionError(f"{name}: {stages[name + '_timeout']}")

    def walk(key, name, condition):
        command("hold", key, lease_ms=15000)
        try:
            wait_for(name, lambda world: condition(world["玩家"]["position"]), 14)
        finally:
            command("release", key)

    def has_event(event):
        nonlocal event_offset
        while True:
            batch = playtest.frames(folder, event_offset, 600)
            if not batch["frames"]:
                return event in events
            events.update(e["kind"] for row in batch["frames"] for e in row["events"])
            event_offset = batch["offset"]

    checkpoint("spawn")
    say(f"{companion}，去东面的应急发电机，把它修好。")
    wait_for("repair_order", lambda a: a[companion].get("action") == "repair")
    # 面朝维修棚出生，往左是闸口。让玩家先到，再检查队友仍在维修期间能否提前结算。
    walk("A", "beside_gate", lambda p: p[2] <= -1.75)
    if actors()["玩家"]["position"][0] > -7.4:
        walk("S", "in_gate", lambda p: p[0] <= -7.4)
    assert not has_event("mission_complete"), "队友仍在外面时不应完成任务"

    wait_for("power_on", lambda a: a["应急发电机"]["health"] == 100, 50)
    wait_for("both_at_gate", lambda _: has_event("mission_complete"), 50)
    final = stages["both_at_gate"]["actors"]
    assert math.dist(final["玩家"]["position"], gate) <= settings["ExitRadius"]
    assert math.dist(final[companion]["position"], gate) <= settings["TeamExitRadius"] and final[companion]["health"] > 0
    screenshot("extracted")

    command("tap", "R")
    wait_for("restart", lambda a: a["应急发电机"]["health"] == 0
             and math.dist(a["玩家"]["position"], stages["spawn"]["actors"]["玩家"]["position"]) < .1)
    screenshot("restart")
    if test_defeat:
        # 站在巡检机前承受伤害。若队友赶到并扶起玩家，继续保留这一段过程。
        walk("W", "approach_guard", lambda p: p[0] > -1.5)
        wait_for("down", lambda a: a["玩家"]["health"] == 0, 50)
        screenshot("down")
        wait_for("defeat", lambda _: has_event("mission_failed"), 100)
        screenshot("defeat")
        command("tap", "R")
        original = stages["spawn"]["actors"]["玩家"]
        wait_for("recover_after_defeat", lambda a: a["玩家"]["health"] == original["health"]
                 and a["玩家"]["inventory"] == original["inventory"] and a["应急发电机"]["health"] == 0
                 and math.dist(a["玩家"]["position"], original["position"]) < .1)
        screenshot("recovered")
    return list(stages)
