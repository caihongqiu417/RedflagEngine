"""从 Sponza 原网格投影中庭上方的两根绳。输出 CSV 逐列给出中心位置与覆盖宽度，NPY 保存各像素的覆盖比例。"""
import argparse
import csv
import json
from pathlib import Path

import numpy as np
import yaml

ROOT = Path(__file__).resolve().parents[1]
SCENE = ROOT / "RedflagForge/assets/scenes/Sponza-Atrium.redflag"


def accessor(model, binary, index, components):
    entry = model["accessors"][index]
    view = model["bufferViews"][entry["bufferView"]]
    element = np.dtype({5123: "<u2", 5125: "<u4", 5126: "<f4"}[entry["componentType"]])
    return np.ndarray((entry["count"], components), element, binary,
                      offset=view.get("byteOffset", 0) + entry.get("byteOffset", 0),
                      strides=(view.get("byteStride", components * element.itemsize), element.itemsize))


def rotation(angles):
    sx, sy, sz = np.sin(angles)
    cx, cy, cz = np.cos(angles)
    return np.array([[cz, -sz, 0], [sz, cz, 0], [0, 0, 1]]) @ np.array(
        [[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]]) @ np.array([[1, 0, 0], [0, cx, -sx], [0, sx, cx]])


def scene_geometry(scene):
    entities = yaml.safe_load(scene.read_text(encoding="utf8"))["Entities"]
    camera = next(e for e in entities if e.get("CameraComponent", {}).get("Primary"))
    sponza = next(e for e in entities if Path(e.get("ModelRendererComponent", {}).get("FilePath", "")).name == "Sponza.gltf")
    file = ROOT / "RedflagForge" / sponza["ModelRendererComponent"]["FilePath"]
    model = json.loads(file.read_text(encoding="utf8"))
    binary = (file.parent / model["buffers"][0]["uri"]).read_bytes()

    # 52 从零开始数，是绳和挂件所在的三角网格。火盆的链条使用另一份带透明贴图的材质。
    mesh = model["meshes"][0]["primitives"][52]
    assert mesh["material"] == 13 and model["materials"][13].get("alphaMode", "OPAQUE") == "OPAQUE"
    transform = sponza["TransformComponent"]
    local = accessor(model, binary, mesh["attributes"]["POSITION"], 3) * model["nodes"][0]["scale"]
    world = (local * transform["Scale"]) @ rotation(transform["Rotation"]).T + transform["Translation"]
    indices = accessor(model, binary, mesh["indices"], 1).reshape(-1, 3)
    return camera, world, indices


def project(width, height, scene, eye):
    camera, world, indices = scene_geometry(scene)
    pose = camera["TransformComponent"]
    forward = -rotation(pose["Rotation"])[:, 2]
    right = np.cross(forward, [0, 1, 0])
    right /= np.linalg.norm(right)
    up = np.cross(right, forward)
    settings = camera["CameraComponent"]["Camera"]
    relative = world - (eye if eye is not None else pose["Translation"])
    depth = relative @ forward
    focal = height / (2 * np.tan(settings["PerspectiveFov"] / 2))
    xy = np.column_stack([width/2 + focal*(relative @ right)/depth, height/2 - focal*(relative @ up)/depth])
    return xy[indices[depth[indices].min(axis=1) > settings["PerspectiveNear"]]]


def covered_pixels(triangles, width, height, samples):
    # 这个范围是默认中庭机位两根绳的中央一段，避开两端挡住绳子的石柱。
    x0, x1 = int(.43*width), int(.57*width) + 1
    y0, y1 = int(.095*height), int(.255*height) + 1
    mask = np.zeros(((y1-y0)*samples, (x1-x0)*samples), dtype=bool)
    origin = np.array([x0, y0])
    for triangle in triangles:
        low = np.maximum(np.ceil((triangle.min(0)-origin)*samples-.5).astype(int), 0)
        high = np.minimum(np.floor((triangle.max(0)-origin)*samples-.5).astype(int)+1, mask.shape[::-1])
        if np.any(low >= high):
            continue
        x = x0 + (np.arange(low[0], high[0])[None, :] + .5) / samples
        y = y0 + (np.arange(low[1], high[1])[:, None] + .5) / samples
        # 采样点在三条边的同侧才被覆盖。重合的三角形只计一次。
        edges = [(b[0]-a[0])*(y-a[1]) - (b[1]-a[1])*(x-a[0]) for a, b in zip(triangle, np.roll(triangle, -1, axis=0))]
        mask[low[1]:high[1], low[0]:high[0]] |= (np.min(edges, axis=0) >= 0) | (np.max(edges, axis=0) <= 0)
    coverage = np.zeros((height, width), np.float32)
    coverage[y0:y1, x0:x1] = mask.reshape(y1-y0, samples, x1-x0, samples).mean(axis=(1, 3))
    return coverage, range(x0, x1)


def strands(coverage, columns):
    rows = np.arange(coverage.shape[0])
    for strand, select in [("near", rows < .17*len(rows)), ("far", rows >= .17*len(rows))]:
        for x in columns:
            area = coverage[select, x]
            total = float(area.sum())
            center = float(area @ (rows[select] + .5) / total) if total else None
            yield strand, x, center, total


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("width", type=int)
    parser.add_argument("height", type=int)
    parser.add_argument("output", type=Path, help="输出文件的路径前缀")
    parser.add_argument("--sample", type=int, default=16, help="每个像素沿一个方向取多少个样点")
    parser.add_argument("--scene", type=Path, default=SCENE)
    parser.add_argument("--eye", nargs=3, type=float, help="相机新的世界坐标，适合小幅平移测试")
    args = parser.parse_args()
    triangles = project(args.width, args.height, args.scene, args.eye)
    coverage, columns = covered_pixels(triangles, args.width, args.height, args.sample)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    np.save(args.output.with_suffix(".npy"), coverage)
    with args.output.with_suffix(".csv").open("w", newline="") as file:
        writer = csv.writer(file)
        writer.writerow(["strand", "x", "center_y", "width_px"])
        writer.writerows(strands(coverage, columns))


if __name__ == "__main__":
    main()
