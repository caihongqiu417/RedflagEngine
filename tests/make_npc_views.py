"""用物理实验台里的箱子和地台生成带相机位姿的多视图输入，单独留两个机位验收重建。"""
import argparse
import json
from pathlib import Path

import numpy as np
import yaml
from PIL import Image


def photograph(boxes, eye, target, width=160, height=128):
    forward = target - eye
    forward /= np.linalg.norm(forward)
    right = np.cross(forward, [0, 1, 0])
    right /= np.linalg.norm(right)
    up = np.cross(right, forward)
    pose = np.eye(4)
    pose[:3] = np.column_stack((right, up, -forward, eye))
    y, x = np.mgrid[:height, :width]
    focal = 145
    rays = (forward + (x[:, :, None] + .5 - width / 2) * right / focal
            - (y[:, :, None] + .5 - height / 2) * up / focal)
    rgb = np.zeros((height, width, 3), dtype=np.uint8)
    depth = np.full((height, width), np.inf)
    for center, extent, color in boxes:
        with np.errstate(divide="ignore"):
            a = (center - extent - eye) / rays
            b = (center + extent - eye) / rays
        near = np.minimum(a, b)
        begin = near.max(axis=2)
        end = np.maximum(a, b).min(axis=2)
        visible = (begin > 0) & (end >= begin) & (begin < depth)
        shade = np.choose(near.argmax(axis=2), [.80, 1.0, .66])
        rgb[visible] = (255 * shade[visible, None] * color).astype(np.uint8)
        depth[visible] = begin[visible]
    valid = np.isfinite(depth)
    points = eye + depth[valid, None] * rays[valid]
    return Image.fromarray(rgb), pose.tolist(), points, rgb[valid]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    out = parser.parse_args().output
    out.mkdir(parents=True, exist_ok=True)
    (out / "images").mkdir(exist_ok=True)
    root = Path(__file__).resolve().parents[1]
    scene = yaml.safe_load((root / "RedflagForge/assets/scenes/Physics-Workshop.redflag").read_bytes())
    boxes = []
    for entity in scene["Entities"]:
        if "BoxCollider3DComponent" in entity:
            tr = entity["TransformComponent"]
            extent = np.array(tr["Scale"]) * entity["BoxCollider3DComponent"]["HalfSize"]
            boxes.append((np.array(tr["Translation"]), extent, entity["MeshRendererComponent"]["BaseColor"][:3]))

    rng = np.random.default_rng(105)
    cloud = []
    for split, count in [("train", 32), ("val", 2)]:
        frames = []
        for i in range(count):
            angle = (i + (0.37 if split == "val" else 0)) * 2 * np.pi / count
            eye = np.array([6*np.cos(angle), 2.6+.7*np.cos(2*angle), 6*np.sin(angle)])
            image, pose, xyz, colors = photograph(boxes, eye, np.array([0., 1.35, 0.]))
            filename = f"images/{split}-{i:02}.png"
            # Brush 0.3 首次读取固定长度的文件头。无压缩的 PNG 也能容纳纯色机位。
            image.save(out / filename, compress_level=0)
            frames.append({"file_path": filename, "transform_matrix": pose})
            if split == "train":
                chosen = rng.choice(len(xyz), size=90, replace=False)
                cloud.extend(np.column_stack((xyz[chosen], colors[chosen])))
        data = {"w": 160, "h": 128, "fl_x": 145, "fl_y": 145, "frames": frames}
        if split == "train":
            data["ply_file_path"] = "points.ply"
        (out / f"transforms_{split}.json").write_text(json.dumps(data), encoding="utf-8")
    with (out / "points.ply").open("w", encoding="ascii") as stream:
        stream.write(f"ply\nformat ascii 1.0\nelement vertex {len(cloud)}\n")
        stream.write("property float x\nproperty float y\nproperty float z\nproperty uchar red\nproperty uchar green\nproperty uchar blue\nend_header\n")
        np.savetxt(stream, cloud, fmt="%.5f %.5f %.5f %u %u %u")


if __name__ == "__main__":
    main()
