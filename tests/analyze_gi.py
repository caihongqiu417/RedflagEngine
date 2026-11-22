"""Summarize linear RGB from the Sponza Vulkan run; map exposure only when writing PNGs."""
import csv
import json
from pathlib import Path

import numpy as np
from PIL import Image

raw = Path("tmp/gi-benchmark")
out = Path("output/profiling/gi")
out.mkdir(parents=True, exist_ok=True)
seeds = [137, 293, 587, 811]
width, height = 160, 90


def read(stem, floats=4):
    data = np.fromfile(raw / (stem + ".f32"), dtype="<f4").reshape(height, width, floats)
    if not np.isfinite(data).all():
        raise ValueError(f"{stem} contains a non-finite result")
    return data.astype(np.float64)


def save_image(name, linear, exposure):
    exposed = 1 - np.exp(-linear * exposure)
    srgb = np.where(exposed < 0.0031308, 12.92*exposed, 1.055*exposed**(1/2.4)-0.055)
    Image.fromarray(np.rint(np.clip(srgb, 0, 1)*255).astype("uint8")).save(out / (name+".png"))


mask = read("surfaces", 12)[:, :, 3] > 0
direct = read("direct-reference-5801")[:, :, :3]
half_a = read("indirect-reference-5801")[:, :, :3]
half_b = read("indirect-reference-9631")[:, :, :3]
indirect = (half_a+half_b)/2
save_image("direct-reference", direct, 4)
save_image("indirect-reference", indirect, 20)
for name, exposure in (("ddgi", 20), ("di", 4), ("gi", 20)):
    if (raw / ("replay-" + name + ".f32")).is_file():
        save_image("capture-" + name, read("replay-" + name)[:, :, :3], exposure)
with (raw/"times.csv").open(encoding="utf-8") as file:
    timings = {row["image"]: row for row in csv.DictReader(file)}

separate, summary = [], []
for method in ("direct-1spp", "direct-8spp", "restir-di", "indirect-1spp", "indirect-8spp", "ddgi", "restir-gi"):
    is_direct = method in ("direct-1spp", "direct-8spp", "restir-di")
    reference = direct if is_direct else indirect
    records = []
    for seed in seeds:
        stem = f"{method}-{seed}"
        img = read(stem)
        rgb = img[:, :, :3]
        if seed == seeds[0]:
            save_image(method, rgb, 4 if is_direct else 20)
        positive = mask & np.any(rgb > 0, axis=2)
        selected = img[:, :, 3] > 0
        timing = timings[stem]
        mse = np.mean((rgb[mask]-reference[mask])**2)
        row = dict(method=method, seed=seed, mse=mse,
                   mean_rgb=np.mean(rgb[mask]),
                   reused_fraction_visible=np.mean(selected[mask]),
                   reused_fraction_positive=np.mean(selected[positive]),
                   positive_fraction_visible=np.mean(positive[mask]),
                   frames=int(timing["frames"]), samples=int(timing["samples_per_frame"]),
                   dispatch_and_wait_ms_per_frame=float(timing["dispatch_and_wait_ms"])/int(timing["frames"]))
        records.append(row)
        separate.append(row)
    summary.append({"method": method, **{k: np.mean([r[k] for r in records]) for k in records[0] if k not in ("method","seed")}})

with (out/"trials.csv").open("w", newline="", encoding="utf-8") as f:
    writer = csv.DictWriter(f, fieldnames=separate[0])
    writer.writeheader()
    writer.writerows(separate)

report = dict(model="Khronos glTF Sample Assets / Sponza",
              model_revision="e124e8d8d7330a5372bc2b4d00d39a4a9d7997ea",
              device=(raw/"device.txt").read_text().strip(), width=width, height=height,
              camera=dict(eye=[-5.5,-2.2,0],target=[6,-1.8,0],vertical_fov_degrees=65),
              visible_pixels=int(mask.sum()), point_lights=16, seeds=seeds,
              direct_reference="exhaustive discrete light sum",
              indirect_reference=dict(samples=1024,seeds=[5801,9631],
                  estimated_mse_floor=np.mean((half_a[mask]-half_b[mask])**2)/4),
              reference_mean_rgb=dict(direct=np.mean(direct[mask]), indirect=np.mean(indirect[mask])),
              ddgi=dict(probe_grid=[12,6,7],directions_per_probe=6,rays_per_direction_per_frame=16),
              reservoirs=dict(fresh_candidates_per_frame=1, temporal=True, spatial_neighbors=4,history_count_cap=16),
              display_exposures=dict(direct=4,indirect=20), results=summary)
(out/"metrics.json").write_text(json.dumps(report, indent=2)+"\n",encoding="utf-8")
for row in summary:
    print(row["method"], "MSE", row["mse"], "reuse (visible/positive)",
          row["reused_fraction_visible"], row["reused_fraction_positive"])
