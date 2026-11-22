"""Compare adjacent GPU frames under the editor's original 320-pixel Sponza settings."""
import csv
import json
from pathlib import Path

import numpy as np
from PIL import Image

raw = Path("tmp/gi-benchmark")
out = Path("output/profiling/gi")
frames, height, width = 24, 180, 320
methods = ["direct", "ddgi-2", "ddgi-16", "di", "gi", "direct-jitter"]
indices = list(csv.DictReader((raw / "temporal-history.csv").open()))


def load(name):
    return np.fromfile(raw / f"temporal-{name}.f32", dtype="<f4").reshape(frames, height, width, 4)[:, :, :, :3]


def display(linear):
    return np.maximum(linear/(1+linear), 0)**(1/2.2)


report = dict(width=width, height=height, frames=frames, seed=137, point_lights=3,
              scene="Sponza-Atrium.redflag, ECS transform and UV textures", results={})
direct = load("direct")
for name in methods:
    image = load(name)
    assert np.isfinite(image).all()
    series = [int(row["history_index"]) for row in indices if row["mode"] == name]
    assert series == list(range(1,frames+1)), (name,series)
    delta = np.diff(image.astype("float64"),axis=0)
    rgb = display(image)
    # 先合成再转显示值，反射光单独调亮容易夸大在 Lit 下能看出的抖动。
    composited = display(direct+image) if name in ("ddgi-2","ddgi-16","gi") else rgb
    shift = np.abs(np.diff(composited.astype("float64"),axis=0))
    # 从第 9、10 帧比到第 23、24 帧，共比较 15 对画面。乘 255 后，差 1 就相当于 8 位颜色里的一级。
    tail = shift[8:] * 255
    report["results"][name] = dict(first_to_second_linear_rms=float(np.sqrt(np.mean(delta[0]**2))),
        steady_linear_rms=float(np.sqrt(np.mean(delta[8:]**2))), steady_display_mean_abs_8bit=float(tail.mean()),
        steady_fraction_any_channel_above_3=float(np.mean(tail.max(axis=3)>3)),
        steady_display_p95_abs_8bit=float(np.percentile(tail,95)), last_history_index=series[-1])
    if name in ("direct","ddgi-2","direct-jitter","gi"):
        map8 = np.clip(tail[-1].max(axis=2)*8,0,255).astype(np.uint8)
        Image.fromarray(map8).save(out / f"temporal-{name}-change.png")
Image.fromarray(np.uint8(np.rint(255*display(direct[-1])))).save(out / "temporal-static-direct.png")
(out / "temporal.json").write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
print(json.dumps(report["results"], indent=2))
