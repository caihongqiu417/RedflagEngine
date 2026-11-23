"""把伸手期间的库存变化对回同一帧的角色剪辑和手腕世界坐标。"""
import json
import math
from station_pose import visible_pose


def verify(session, output):
    phases = {"pickup": [], "give": []}
    for line in (session/"frames.jsonl").read_bytes().split(b"\n")[:-1]:
        frame = json.loads(line)
        actors = {a["id"]:a for a in frame["actors"]}
        watt, player = actors["1102"], actors["1101"]
        stage = watt.get("stage")
        if stage not in phases: continue
        motion = visible_pose(watt)
        phases[stage].append(dict(seq=frame["seq"],time=motion["time"],clip=motion["clip"],
                                  wrist=motion["nodes"]["hand.L"],position=watt["position"],
                                  watt_kits=watt["inventory"].get("medkit",0),
                                  player_kits=player["inventory"].get("medkit",0)))
    result = {}
    for stage, rows in phases.items():
        assert len(rows)>=3, (stage,len(rows))
        clip = "Station-Pickup" if stage=="pickup" else "Station-Give"
        assert all(row["clip"] == clip for row in rows)
        reach = max(math.dist(row["wrist"],rows[0]["wrist"]) for row in rows)
        assert reach > .3, (stage,reach)
        change = next(i for i in range(1,len(rows)) if rows[i]["watt_kits"]!=rows[i-1]["watt_kits"])
        before, after = rows[change-1:change+1]
        assert after["watt_kits"] - before["watt_kits"] == (1 if stage=="pickup" else -1)
        if stage == "give": assert after["player_kits"] - before["player_kits"] == 1
        result[stage] = dict(max_wrist_travel_m=reach, inventory_frame=after["seq"], frames=rows)
    output.write_text(json.dumps(result,indent=2)+"\n",encoding="utf8")
