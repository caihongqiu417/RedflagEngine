"""从项目根目录运行。直接按三角形计算覆盖率，用来独立核对相机取样；不包含贴图、光照和 TAA 着色器。"""
import json

import numpy as np

from project_atrium_wires import ROOT, SCENE, covered_pixels, project

OUT = ROOT / "output/validation/atrium-aa"


def halton(index, base):
    value, place = 0., 1.
    while index:
        place /= base
        value += (index % base) * place
        index //= base
    return value


def static_camera(width, height):
    triangles = project(width, height, SCENE, None)
    x0, x1 = int(.43*width), int(.57*width) + 1
    y0, y1 = int(.095*height), int(.255*height) + 1
    bounds_min, bounds_max = triangles.min(1), triangles.max(1)
    triangles = triangles[((bounds_max[:, 0] >= x0-1) & (bounds_min[:, 0] <= x1+1)) &
                          ((bounds_max[:, 1] >= y0-1) & (bounds_min[:, 1] <= y1+1))]

    def raster(offset, side=1):
        # JitterProjection 的 Y 偏移取负，Vulkan 视口又翻转 Y；在左上为原点的图里，两轴的几何偏移均是 +(Halton-.5)。
        pixels, _ = covered_pixels(triangles + offset, width, height, side)
        return pixels[y0:y1, x0:x1]

    reference = raster(np.zeros(2), 32)

    def errors(pixels):
        result = {}
        rows = np.arange(y0, y1)
        for name, band in [("near", rows < .17*height), ("far", rows >= .17*height)]:
            ideal, actual = reference[band], pixels[band]
            touched = ideal > 0
            result[name] = dict(touched_pixels=int(touched.sum()),
                                coverage_MAE=float(abs(actual-ideal)[touched].mean()), area_px=float(actual.sum()))
        return result

    offsets = np.array([[halton(i, 2), halton(i, 3)] for i in range(1, 17)]) - .5
    single = np.array([raster(offset) for offset in offsets])
    report = dict(size=[width, height], reference=errors(reference), no_jitter=errors(raster(np.zeros(2))),
                  halton_frames=[], variants={"orig_16_halton": errors(single.mean(0))})
    history = single[0]
    for i in range(32):
        weight = min(.85, i/(i+1))
        history = weight * history + (1-weight) * single[i % 16]
        report["halton_frames"].append(dict(count=i+1, jitter_px=offsets[i % 16].tolist(),
            single=errors(single[i % 16]), uniform=errors(single[np.arange(i+1) % 16].mean(0)), ema_85=errors(history)))

    # 对照方案：把像素沿 X 和 Y 各分为 16 个窄条，每条取一次中心。改变步长能比较排列本身造成的误差。
    for step in [5, 7, 9, 11, 13]:
        probes = [raster((np.array([(i * step) % 16, i]) + .5) / 16 - .5) for i in range(16)]
        report["variants"][f"rook_{step}"] = errors(np.mean(probes, axis=0))
    return report


def moving_camera():
    width, height = 600, 385
    report = dict(scene=SCENE.name, mesh_index=0, primitive_index=52, material_index=13, width=width, height=height,
                  camera_step_world=[0, 0, .0055], subsamples_per_axis=32, frames=[],
                  quantity="CPU projected geometry only; independent of shading, TAA and frame timing.")
    previous = None
    for frame in range(17):
        eye = [-5.5, -2.20000005, frame * .0055]
        pixels, columns = covered_pixels(project(width, height, SCENE, eye), width, height, 32)
        current = dict(i=frame, eye=eye, threshold_columns=[columns.start, columns.stop], bands={})
        for name, band in [("near", slice(36, 65)), ("far", slice(65, 99))]:
            region = (band, slice(columns.start, columns.stop))
            area = pixels[region]
            row = dict(area_px=float(area.sum()), max_coverage=float(area.max()))
            if previous is not None:
                row["coverage_delta_px"] = float(np.abs(area - previous[region]).sum())
            current["bands"][name] = row
        report["frames"].append(current)
        previous = pixels
    return report


if __name__ == "__main__":
    OUT.mkdir(parents=True, exist_ok=True)
    for name, report in [("motion-geometry", moving_camera()),
                         ("halton-geometry", {str(w): static_camera(w, h) for w, h in [(600, 385), (1391, 892)]})]:
        (OUT / (name + ".json")).write_text(json.dumps(report, indent=2) + "\n", encoding="utf8")
