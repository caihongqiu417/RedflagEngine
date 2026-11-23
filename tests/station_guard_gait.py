"""比较运行中巡检机的实体位置、行走根骨和左后脚，跨过一次完整步态循环。"""
import hashlib
import json
import math

from check_forge_play import wait_for


def arrange(scene):
    from station_melee import arrange as trim_scene
    guard = next(row for row in scene["Entities"] if row["Entity"] == 1103)
    watt = next(row for row in scene["Entities"] if row["Entity"] == 1102)
    script = guard["ScriptComponent"]
    npc = watt["NpcComponent"]
    trim_scene(scene, "movement")
    guard["ScriptComponent"] = script
    guard["TransformComponent"]["Translation"] = [-2.0, .08, 1.4]
    watt["NpcComponent"] = npc
    watt["TransformComponent"]["Translation"] = [-6.9, .08, -.1]
    recorder = next(row for row in scene["Entities"] if row["Entity"] == 1900)
    recorder["ScriptComponent"]["ScriptFields"].append({"Name":"FinishGuardEncounter","Type":"Bool","Value":True})


def verify(session, process, exe, output):
    stream = session/"frames.jsonl"

    def read():
        if not stream.is_file():
            return []
        rows = []
        for line in stream.read_bytes().split(b"\n")[:-1]:
            frame = json.loads(line)
            guard = next(actor for actor in frame["actors"] if actor["id"] == "1103")
            player = next(actor for actor in frame["actors"] if actor["id"] == "1101")
            watt = next(actor for actor in frame["actors"] if actor["id"] == "1102")
            animation = guard.get("animation", {})
            nodes = animation.get("nodes", {})
            if "MASTER" in nodes and "L-RearFoot" in nodes:
                rows.append(dict(seq=frame["seq"], seconds=frame["seconds"], position=guard["position"],
                                 master=nodes["MASTER"], foot=nodes["L-RearFoot"], body=nodes["Body"],
                                 clip=animation["clip"], time=animation["time"], guard_health=guard["health"],
                                 player_health=player["health"], watt_stage=watt.get("stage"), watt_action=watt.get("action"),
                                 watt_clip=watt["animation"]["clip"], wrist=watt["animation"]["nodes"].get("hand.L")))
        return rows

    def reached():
        rows = read()
        if (any(row["player_health"] == 0 and row["watt_stage"] == "revive" for row in rows)
                and any(row["watt_action"] == "follow" and row["player_health"] == 50 for row in rows)):
            return rows

    rows = wait_for(reached, process, 45)
    walk = [row for row in rows if row["clip"] == "Walk-cycle"]
    xz = lambda p: (p[0], p[2])
    travel = math.dist(xz(walk[0]["position"]), xz(walk[-1]["position"]))
    root_error = max(math.dist(xz(row["master"]), xz(row["position"])) for row in walk)
    wraps = sum(b["time"] < a["time"] for a, b in zip(walk, walk[1:]))
    foot_height = max(row["foot"][1] for row in walk) - min(row["foot"][1] for row in walk)
    assert travel > 1.8 and wraps >= 1 and root_error < .005 and foot_height > .05, (
        len(walk), travel, wraps, root_error, foot_height)
    broken = [row for row in rows if row["clip"] == "Damage_heavy"]
    rescue = [row for row in rows if row["watt_stage"] == "revive"]
    assert any(row["clip"] == "Cannon_Fire_Recoil" for row in rows)
    assert len(broken) >= 3 and all(row["guard_health"]==0 and abs(row["time"]-.3)<.001 for row in broken)
    assert max(math.dist(row["body"],broken[0]["body"]) for row in broken)<.001
    assert all(row["watt_clip"] == "Station-Pickup" for row in rescue)
    reach = max(math.dist(row["wrist"],rescue[0]["wrist"]) for row in rescue)
    assert reach > .3, reach
    result = dict(exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(), walk_samples=len(walk),
                  travel_m=travel, gait_wraps=wraps, max_root_horizontal_error_m=root_error,
                  foot_vertical_range_m=foot_height, broken_frames=len(broken), rescue_samples=len(rescue),
                  rescue_wrist_travel_m=reach, final_clip=rows[-1]["clip"], frames=rows)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, indent=2)+"\n", encoding="utf8")
    print(f"PASS guard moved {travel:.3f} m, root error {root_error:.6f} m, left foot lifted {foot_height:.3f} m")
