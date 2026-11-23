"""升降一块真实的 PhysX 隔板，记录巡检机丢失目标、取消蓄能和恢复巡逻的过程。"""
import hashlib
import json

from check_forge_play import wait_for


def stage_cover(scene):
    entities = {row["Entity"]: row for row in scene["Entities"]}
    guard = entities[1103]
    guard["TransformComponent"]["Translation"] = [-5.05, .08, 1.4]
    entities[1930]["TransformComponent"]["Translation"] = [-5.05, .08, 1.4]
    entities[1102].pop("NpcComponent")
    entities[1102]["TransformComponent"]["Translation"] = [-12, .08, -10]

    # 步态测试会在放电后主动结束战斗。隔板测试需要同一台巡检机继续运行。
    recorder = entities[1900]["ScriptComponent"]["ScriptFields"]
    recorder[:] = [field for field in recorder if field["Name"] != "FinishGuardEncounter"]
    targets = [dict(Name=name, Type="Entity", Value=uid)
               for name, uid in (("Guard", 1103), ("Player", 1101), ("Lamp", 1931))]
    scene["Entities"].append({
        "Entity": 1973,
        "TagComponent": {"Tag": "测试隔板"},
        "TransformComponent": {"Translation": [-5.975, .575, 1.4], "Rotation": [0, 0, 0], "Scale": [1, 1, 1]},
        "RigidBody3DComponent": {"BodyType": "Kinematic", "Density": 1},
        "BoxCollider3DComponent": {"HalfSize": [.07, .70, 3]},
        "ScriptComponent": {"Class": "StationTest.SentinelCoverProbe", "ScriptFields": targets},
    })


def verify(session, process, exe, output):
    path = session / "frames.jsonl"

    def finished():
        if not path.exists():
            return None
        rows = [json.loads(line) for line in path.read_bytes().split(b"\n")[:-1]]
        if any(e["kind"] == "cover_probe" and e["text"] == "finished" for r in rows for e in r["events"]):
            return rows

    rows = wait_for(finished, process, 44)
    timeline = []
    for row in rows:
        actors = {actor["id"]: actor for actor in row["actors"]}
        for event in row["events"]:
            if event["kind"] in ("cover_probe", "sentinel", "sentinel_pulse", "sentinel_target"):
                timeline.append(dict(seq=row["seq"], seconds=row["seconds"], frame_ms=row["frame_ms"],
                                     kind=event["kind"], value=event["text"], player_health=actors["1101"]["health"],
                                     guard_health=actors["1103"]["health"]))

    observed = {e["value"]: e for e in timeline if e["kind"] == "cover_probe"}
    pulses = [e for e in timeline if e["kind"] == "sentinel_pulse"]
    assert [e["value"] for e in pulses] == ["1973", "1101"], pulses
    assert [e["player_health"] for e in pulses] == [100, 88]

    actions = [e for e in timeline if e["kind"] == "sentinel"]
    hidden = [e["value"] for e in actions if observed["hide_during_charge"]["seq"] <= e["seq"] <= observed["reappear"]["seq"]]
    assert "search" in hidden and "patrol" in hidden and hidden.index("search") < hidden.index("patrol"), hidden
    fresh = next(e for e in actions if e["value"] == "charge" and e["seq"] >= observed["reappear"]["seq"])
    assert pulses[1]["seconds"] - fresh["seconds"] + fresh["frame_ms"] / 1000 >= .8 - .005
    assert actions[-1]["value"] == "damaged" and actions[-1]["guard_health"] == 0
    assert rows[-1]["seq"] > actions[-1]["seq"] and observed["finished"]["player_health"] == 88

    report = dict(exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
                  scene="隔板测试场景，正式角色脚本与优先行为树", low_cover_top_m=1.275,
                  raised_cover_top_m=1.93, charge_seconds=.8, timeline=timeline)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf8")
    print("PASS low cover intercepted the pulse, target loss cancelled charging, search returned to patrol, damage won priority")
