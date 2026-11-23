"""直接量 Operator 蒙皮表面到模型脚底平面的距离；场景地形与物理另用实机测。"""
import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
from check_station_soles import Rig, read_glb, transforms


def check(path):
    rig = Rig(*read_glb(path))
    skin = rig.skin_vertices(shoes_only=False)
    groups = []
    for joints, weights, _ in skin:
        dominant = joints[np.arange(len(joints)), weights.argmax(axis=1)]
        groups.extend(rig.nodes[j]["name"] for j in dominant)
    groups = np.asarray(groups)
    bounds = {}
    clips = {}
    for name in ("Idle-cycle", "Station-Crouch", "Station-CrouchWalk", "Station-Down", "Station-GetUp"):
        clip = rig.clip(name)
        end = max(float(rig.values(s["input"])[-1, 0]) for s in clip["samplers"])
        times = np.linspace(0, end, round(end * 120) + 1)
        pose = rig.pose(clip, times)
        root = rig.names["root"]
        pose[root]["translation"] = np.tile(rig.nodes[root].get("translation", [0, 0, 0]), (len(times), 1))
        world = transforms(rig.world(pose))
        lowest, highest = [], []
        # 分段蒙皮，避免一次保留上千帧的所有顶点。所有网格、所有四个关节权重都参与计算。
        for start in range(0, len(times), 8):
            rows = world[start:start+8, :, 1, :]
            heights = [np.einsum("fvki,vki,vk->fv", rows[:, joints], local, weights)
                       for joints, weights, local in skin]
            lowest.extend(np.min([h.min(axis=1) for h in heights], axis=0))
            highest.extend(np.max([h.max(axis=1) for h in heights], axis=0))
        clips[name] = dict(samples=len(times), min_support_m=min(lowest), max_support_m=max(lowest),
                           max_height_m=max(highest))
        if name == "Station-Down":
            points = np.concatenate([np.einsum("vkij,vkj,vk->vi", world[-1, joints], local, weights)
                                     for joints, weights, local in skin])
            for group, mask in (("body", np.ones(len(groups), bool)), ("chest", groups == "chest"),
                                ("backpack", np.char.startswith(groups, "boosterpack")), ("hips", groups == "hips"),
                                ("head", groups == "head.001"), ("boots", np.char.startswith(groups, "foot") |
                                 np.char.startswith(groups, "toe"))):
                bounds[group] = dict(min=points[mask].min(axis=0).tolist(), max=points[mask].max(axis=0).tolist())
    # 素材按 30 Hz 写入关键帧。两帧之间用 120 Hz 取样，身体的最低点需保持在地面上下 1 厘米内。
    for name, entry in clips.items():
        assert max(abs(entry["min_support_m"]), abs(entry["max_support_m"])) < .01, name
    return dict(model_sha256=hashlib.sha256(path.read_bytes()).hexdigest(), unit="m", hz=120,
                clips=clips, down_pose_bounds=bounds)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("model", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    result = check(args.model)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf8")
