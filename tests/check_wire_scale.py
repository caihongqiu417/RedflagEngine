"""从根目录运行 python tests/check_wire_scale.py。用原模型两条绳和两条已知宽度的直线比较单帧空间取样。"""
import json
import math

import numpy as np

from project_atrium_wires import ROOT, SCENE, project


def area_filter(pixels, size, axis):
    """每个输出像素的宽度或高度在源图上占一个区间，按重合的长度加权。"""
    source = np.moveaxis(pixels, axis, 0)
    scale = source.shape[0] / size
    left = np.arange(size) * scale
    first = np.floor(left).astype(int)
    result = np.zeros((size, source.shape[1]), np.float32)
    for offset in range(math.ceil(scale) + 1):
        index = first + offset
        length = np.maximum(0, np.minimum(left + scale, index + 1) - np.maximum(left, index))
        result += (length / scale)[:, None] * source[np.minimum(index, source.shape[0] - 1)]
    return np.moveaxis(result, 0, axis)


def raster(triangles, target_width, target_height, output_width, output_height, bounds):
    # 同一份相机投影覆盖同一个面板。源图尺寸取整之后，X 和 Y 各按实际比例换算。
    ratio = np.array([target_width / output_width, target_height / output_height])
    x0, x1, y0, y1 = bounds
    corners = np.array([x0, y0]) * ratio
    origin = np.floor(corners).astype(int)
    limit = np.ceil(np.array([x1, y1]) * ratio).astype(int)
    width, height = limit - origin
    mask = np.zeros((height, width), np.float32)
    verts = triangles * ratio - origin
    low = np.maximum(np.ceil(verts.min(axis=1) - .5).astype(int), 0)
    high = np.minimum(np.floor(verts.max(axis=1) - .5).astype(int) + 1, [width, height])
    for index in np.nonzero(np.all(high > low, axis=1))[0]:
        ax, ay = low[index]
        bx, by = high[index]
        x = np.arange(ax, bx)[None, :] + .5
        y = np.arange(ay, by)[:, None] + .5
        tri = verts[index]
        sides = [(b[0]-a[0])*(y-a[1]) - (b[1]-a[1])*(x-a[0]) for a, b in zip(tri, np.roll(tri, -1, axis=0))]
        inside = (np.min(sides, axis=0) >= 0) | (np.max(sides, axis=0) <= 0)
        mask[ay:by, ax:bx][inside] = 1.
    return mask, origin


def render(triangles, width, height, scale, bounds):
    sw, sh = math.ceil(width * scale), math.ceil(height * scale)
    mask, origin = raster(triangles, sw, sh, width, height, bounds)
    x0, x1, y0, y1 = bounds
    if scale == int(scale):
        side = int(scale)
        return mask.reshape(y1-y0, side, x1-x0, side).mean((1, 3))
    # 局部图的左上角一般不对齐输出像素。把它放回原网格再缩图，避免每一行条纹错开半个像素。
    full = np.zeros((sh, sw), np.float32)
    ox, oy = origin
    full[oy:oy+mask.shape[0], ox:ox+mask.shape[1]] = mask
    return area_filter(area_filter(full, width, 1), height, 0)[y0:y1, x0:x1]


def compare(frames, width, height, bounds):
    truth = np.array([render(t, width, height, 32, bounds) for t in frames])
    edge = (truth > 0) & (truth < 1)
    changed_edges = edge[:-1] | edge[1:]
    areas = truth.sum(axis=(1, 2))
    result = {}
    for scale in (1, 1.5, 2):
        measured = np.array([render(t, width, height, scale, bounds) for t in frames])
        diff = measured - truth
        ratio = measured.sum(axis=(1, 2)) / areas
        result[str(scale)] = dict(source=[math.ceil(width*scale), math.ceil(height*scale)],
                                  edge_mae=float(np.abs(diff[edge]).mean()),
                                  motion_delta_mae=float(np.abs(np.diff(diff, axis=0)[changed_edges]).mean()),
                                  area_ratio_range=[float(ratio.min()), float(ratio.max())])
    return dict(output=[width,height], bounds_xyxy=[bounds[0], bounds[2], bounds[1], bounds[3]], results=result)


def synthetic(width_px):
    frames = []
    width, height = 128, 65
    for i in range(17):
        # 线与水平线的斜率为 0.12，每帧沿上下方向移动 1/8 像素；width_px 是垂直于线的宽度。
        x = np.array([0., width])
        y = 20. + .12*x + i/8
        half = width_px * math.sqrt(1+.12**2) / 2
        quad = np.array([[x[0], y[0]-half], [x[1], y[1]-half], [x[1], y[1]+half], [x[0], y[0]+half]])
        frames.append(quad[[[0,1,2],[0,2,3]]])
    return compare(frames, width, height, (8, 120, 18, 40))


if __name__ == "__main__":
    report = dict(reference_subsamples_per_axis=32, frame_count=17, sky_luminance=0, wire_luminance=1,
                  jitter=False, geometry_only=True, ceiling_for_noninteger_source_size=True, cases={})
    for width, height in [(600, 385), (1391, 892)]:
        frames = [project(width, height, SCENE, [-5.5, -2.20000005, .0055*i]) for i in range(17)]
        for name, a, b in [("near", .095, .17), ("far", .17, .255)]:
            bounds = int(.43*width), int(.57*width)+1, int(a*height), int(b*height)
            report["cases"][f"sponza_{width}_{name}"] = compare(frames, width, height, bounds)
    for thickness in [.35, .85]:
        report["cases"][f"line_{thickness}"] = synthetic(thickness)
    path = ROOT / "output/validation/atrium-aa/scale-coverage.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf8")
