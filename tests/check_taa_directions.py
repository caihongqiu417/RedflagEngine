"""从仓库根目录运行。把实际采样点覆盖的比例，与细线在一个像素内的精确面积比较。"""
import json
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]


def halton(index, base):
    result, scale = 0., 1.
    while index:
        index, digit = divmod(index, base)
        scale /= base
        result += digit * scale
    return result


def area_below(limit, a, b):
    # 两个方向在像素内各长 1。沿法线投影后，求出的面积就是两个均匀分布相加的累积分布。
    a, b = sorted((abs(a), abs(b)), reverse=True)
    if b < 1.e-10:
        return np.clip(limit / a + .5, 0., 1.)
    def positive_square(x):
        return np.maximum(x, 0.) ** 2

    t = limit + (a + b) / 2
    return (positive_square(t) - positive_square(t-a) - positive_square(t-b)
            + positive_square(t-a-b)) / (2*a*b)


def measure(points):
    distances = np.linspace(-1.5, 1.5, 385)
    by_angle = []
    for degrees in range(180):
        a, b = np.cos(np.deg2rad(degrees)), np.sin(np.deg2rad(degrees))
        projected = (points - .5) @ np.array([a, b])
        errors = []
        for width in (.2, .5, 1., 1.5):
            lo, hi = distances-width/2, distances+width/2
            area = area_below(hi, a, b) - area_below(lo, a, b)
            partial = (area > .00001) & (area < .99999)
            sample = ((projected >= lo[:, None]) & (projected < hi[:, None])).mean(axis=1)
            errors.extend(np.abs(sample[partial] - area[partial]))
        by_angle.append(np.mean(errors))
    return dict(mean_mae=float(np.mean(by_angle)), worst_mae=max(by_angle),
                worst_direction_degrees=int(np.argmax(by_angle)), per_degree_mae=by_angle)


if __name__ == "__main__":
    old = np.array([(halton(i,2), halton(i,3)) for i in range(1,17)])
    rows = np.arange(16)
    lattice = (np.column_stack((7*rows % 16, rows)) + .5) / 16
    # Vulkan 标准 16 样点表。整体平移半个小格，时间采样的平均位置恰在像素中心。
    vk = (np.array([(9,9),(7,5),(5,10),(12,7),(3,6),(10,13),(13,11),(11,3),
                    (6,14),(8,1),(4,2),(2,12),(0,8),(15,4),(14,15),(1,0)])+.5)/16
    rotated = np.column_stack((1-vk[:,1], vk[:,0]))
    two_cycles = np.concatenate([vk, rotated])
    assert len(np.unique(two_cycles, axis=0)) == 32
    patterns = {"halton16": old, "lattice7": lattice, "centered_vulkan16": vk,
                "centered_vulkan32": two_cycles}
    results = {name: measure(points) for name, points in patterns.items()}
    report = dict(widths_px=[.2, .5, 1., 1.5], offset_count=385, direction_count=180,
                  quantity="Mean absolute error on partially covered pixels, using exact area. Each direction gets equal weight.",
                  standard="https://docs.vulkan.org/spec/latest/chapters/primsrast.html#primsrast-standardsamplelocations",
                  results=results)
    folder = ROOT / "output/validation/atrium-aa"
    folder.mkdir(exist_ok=True, parents=True)
    (folder/"strip-directions.json").write_text(json.dumps(report,indent=2)+"\n",encoding="utf8")
    for name, values in results.items():
        print(name, values["mean_mae"], values["worst_mae"], values["worst_direction_degrees"])
