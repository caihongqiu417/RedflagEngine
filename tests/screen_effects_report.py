"""Verify linear GPU readbacks, then generate display images for the notes."""
from pathlib import Path
import json
import numpy as np
from PIL import Image

root = Path(__file__).resolve().parents[1]
raw = root / "tmp/screen-effects"
out = root / "output/screen-effects"
out.mkdir(parents=True, exist_ok=True)
width, height = 161, 91


def load(name, channels=4):
    image = np.fromfile(raw / f"{name}.f32", dtype="<f4").reshape(height, width, channels)
    if not np.isfinite(image).all():
        raise ValueError(f"Nonfinite pixels in {name}")
    return image


def publish(name, rgb, linear=True):
    rgb = np.maximum(rgb, 0)
    if linear:
        # 所有彩色图用同一曝光。误差在这一步之前计算。
        rgb = rgb * 4
        rgb = rgb / (1 + rgb)
        rgb = np.where(rgb <= .0031308, 12.92 * rgb, 1.055 * rgb ** (1 / 2.4) - .055)
    Image.fromarray(np.uint8(np.clip(rgb, 0, 1) * 255 + .5)).save(out / f"{name}.png")


def mse(a, b):
    return float(np.mean((a[valid, :3].astype(float) - b[valid, :3]) ** 2))


surfaces = load("surfaces", 12)
valid = surfaces[:, :, 3] > 0
direct, ao, ssr, taa, clusters = (load(n) for n in ("direct", "gtao", "ssr", "taa-last", "clustered"))
occlusion = ao[:, :, 3]
hits = ssr[:, :, 3] > 0
samples = [load(f"jitter-{i}") for i in range(8)]
average = np.mean(samples, axis=0)
reference = load("cluster-reference")
difference = np.abs(clusters[:, :, :3] - reference[:, :, :3])
first = load("taa-first")
moved = load("taa-moved")
tiny = load("gtao-tiny-radius")[:, :, 3]
stats = {
    "scene": "Khronos Sponza", "width": width, "height": height,
    "camera_eye": [-5.5, -2.2, 0], "camera_target": [6, -1.8, 0], "vertical_fov_degrees": 65,
    "display_exposure": 4, "device": (raw / "device.txt").read_text().strip(),
    "visible_pixels": int(valid.sum()), "lights_for_cluster_test": 49,
    "ao_min": float(occlusion[valid].min()), "ao_mean": float(occlusion[valid].mean()),
    "ao_negligible_radius_mean": float(tiny[valid].mean()),
    "ssr_hit_fraction": float(hits[valid].mean()),
    "ssr_repeat_max_difference": float(np.max(np.abs(ssr - load("ssr-repeat")))),
    "ssr_roughness_image_mse": mse(ssr, load("ssr-rough")),
    "taa_first_rgb_mse": mse(first, samples[0]), "taa_reset_max_weight": float(load("taa-reset")[:, :, 3].max()),
    "taa_frame8_mean_weight": float(taa[valid, 3].mean()),
    "taa_moved_frame_reused_fraction": float(np.mean(moved[:, :, 3] > .001)),
    "single_frame_vs_eight_jitters_mse": mse(samples[-1], average), "taa_vs_eight_jitters_mse": mse(taa, average),
    "cluster_max_absolute_rgb_error": float(difference.max()),
    "cluster_mean_candidates": float(clusters[valid, 3].mean()), "cluster_zero_lights_rgb_max": float(load("cluster-empty").max()),
}
assert valid.sum() > width * height / 2
assert 0 <= occlusion.min() <= occlusion.max() <= 1 and occlusion[valid].mean() < tiny[valid].mean()
assert hits.any() and stats["ssr_repeat_max_difference"] == 0 and stats["ssr_roughness_image_mse"] > 0
assert stats["taa_first_rgb_mse"] < 1e-12 and stats["taa_reset_max_weight"] == 0
assert .05 < stats["taa_frame8_mean_weight"] <= .91
assert 0 < stats["taa_moved_frame_reused_fraction"] < 1
assert np.allclose(clusters[:, :, :3], reference[:, :, :3], rtol=2e-5, atol=2e-5)
assert stats["cluster_mean_candidates"] < 49 and stats["cluster_zero_lights_rgb_max"] == 0
for name, rgb in (("direct", direct), ("ssr", ssr), ("taa", taa), ("clustered", clusters)):
    publish(name, rgb[:, :, :3])
publish("ao-visibility", np.repeat(occlusion[:, :, None], 3, axis=2), False)
publish("ssr-hits", np.repeat(hits[:, :, None], 3, axis=2), False)
publish("taa-history", np.repeat(moved[:, :, 3:4], 3, axis=2), False)
publish("cluster-candidates", np.repeat(clusters[:, :, 3:4] / 49, 3, axis=2), False)
(out / "metrics.json").write_text(json.dumps(stats, indent=2) + "\n", encoding="utf8")
print(json.dumps(stats, indent=2))
