"""把按键回执、世界坐标和屏幕上的骨盆放在同一条时间线上，并保存原速实机录像。"""
import argparse
import csv
import json
import math
from pathlib import Path

from capture_station_routes import video


def review(folder, output, name):
    take = json.loads((folder / "take.json").read_bytes())
    frames = [json.loads(line) for line in (folder / "frames.jsonl").read_bytes().splitlines()]
    inputs = {item["phase"]: item["applied_seq"] for item in take["inputs"]}
    rows = {}
    for frame in frames:
        if frame["seconds"] < take["origin"]:
            continue
        player = next(a for a in frame["actors"] if a["id"] == "1101")
        camera = next(a for a in frame["actors"] if a["id"] == "1100")
        position = player["position"]
        hips = player["animation"]["nodes"]["hips"]
        pose = player["animation"]
        upper = next((layer for layer in pose.get("montages",[]) if layer["slot"] == "UpperBody" and
                      layer["state"] == "fire"), {})
        rows[frame["seq"]] = dict(seq=frame["seq"], seconds=frame["seconds"] - take["origin"],
            clip=pose["clip"], clip_time=pose["time"], x=position[0], z=position[2], hip_x=hips[0], hip_z=hips[2],
            right_hand_y=pose["nodes"]["hand.R"][1], left_foot_y=pose["nodes"]["foot.L"][1],
            right_foot_y=pose["nodes"]["foot.R"][1], upper_clip=upper.get("clip",""),
            upper_weight=upper.get("weight",0), fading=upper.get("fading",False),
            camera_x=camera["position"][0], camera_z=camera["position"][2],
            input=" ".join(f"{i['op']} {i.get('key', '')}" for i in frame["inputs"]),
            events=" ".join(e["kind"] for e in frame["events"]))

    first_idle = next(r for i, r in rows.items() if i > inputs["stop"] and r["clip"] == "Idle-cycle")
    walking = rows[first_idle["seq"] - 1]
    idle = next(r for r in rows.values() if r["seconds"] >= first_idle["seconds"] + .2)
    dx = (idle["hip_x"] - idle["x"]) - (walking["hip_x"] - walking["x"])
    dz = (idle["hip_z"] - idle["z"]) - (walking["hip_z"] - walking["z"])
    reverse = rows[inputs["leave-boundary"] - 1]
    recovered = rows[inputs["stop-again"]]
    metrics = dict(hips_backward_m=-dx, hips_horizontal_shift_m=math.hypot(dx, dz),
        root_advance_during_blend_m=idle["x"]-walking["x"],
        backward_travel_m=rows[inputs["backward"]-1]["x"]-rows[inputs["stop-backward"]]["x"],
        moved_away_from_wall_m=recovered["x"]-reverse["x"],
        stop_sample_frames=[walking["seq"], idle["seq"]], wall_sample_frames=[reverse["seq"], recovered["seq"]],
        pulse_frames=[r["seq"] for r in rows.values() if "pulse" in r["events"].split()],
        fire_pose_frames=[r["seq"] for r in rows.values() if r["clip"] == "Cannon_Charge" or r["upper_weight"] > 0])
    if "fire-while-walking" in inputs:
        overlap = [r for r in rows.values() if r["clip"] == "walking_gun-cycle" and r["upper_weight"] > 0]
        assert len(overlap) >= 2 and overlap[-1]["clip_time"] != overlap[0]["clip_time"]
        metrics.update(moving_fire_frames=[r["seq"] for r in overlap],
            walking_during_shot_m=overlap[-1]["x"]-overlap[0]["x"],
            left_foot_height_change_m=max(r["left_foot_y"] for r in overlap)-min(r["left_foot_y"] for r in overlap),
            right_hand_height_change_m=max(r["right_hand_y"] for r in overlap)-min(r["right_hand_y"] for r in overlap))
        assert metrics["walking_during_shot_m"] > .1
    with (output / (name+".csv")).open("w", newline="", encoding="utf-8-sig") as f:
        writer = csv.DictWriter(f, fieldnames=walking.keys())
        writer.writeheader()
        writer.writerows(rows.values())
    video([(c["elapsed"], folder/"images"/f"{c['seq']:05}.jpg") for c in take["captures"]], output/(name+".mp4"))
    return dict(**metrics, inputs=inputs, source={k: take[k] for k in (
        "archive", "archive_sha256", "exe_sha256", "replaced_native", "gameplay_sha256", "scene_sha256", "render_size")})


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("before", "after", "output"):
        parser.add_argument(name, type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    result = {name: review(getattr(args,name), args.output, name) for name in ("before","after")}
    (args.output / "result.json").write_text(json.dumps(result, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
    before, after = result["before"], result["after"]
    assert before["hips_backward_m"] > .15 and after["hips_horizontal_shift_m"] < .035
    assert before["moved_away_from_wall_m"] < .05 and after["moved_away_from_wall_m"] > 1
    assert after["backward_travel_m"] > 5 and after["pulse_frames"] and after["fire_pose_frames"]
