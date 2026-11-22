"""Convert the five replay outputs from replay_screen_effects.py into the figures in the performance book."""
import json
from pathlib import Path

import numpy as np
from PIL import Image

raw = Path("tmp/screen-effects")
out = Path("output/profiling/screen-effects")
report = json.loads((out / "renderdoc.json").read_text())
assert report["result"] == "ok"
width, height = 161, 91
visible = np.fromfile(raw / "surfaces.f32", dtype="<f4").reshape(height, width, 12)[:, :, 3] > 0
stats = {}
for name in ("gtao", "ssr", "taa", "clustered"):
    frame = np.fromfile(raw / ("replay-" + name + ".bin"), dtype="<f4").reshape(height, width, 4)
    assert np.isfinite(frame).all()
    alpha = frame[:, :, 3]
    if name != "taa":
        baseline = np.fromfile(raw / (name + ".f32"), dtype="<f4").reshape(frame.shape)
        stats[name + "_baseline_max_diff"] = float(np.abs(frame - baseline).max())
        assert np.allclose(frame, baseline, rtol=2e-5, atol=2e-5)
    stats[name + "_mean_alpha_on_surface"] = float(alpha[visible].mean())
    mask = (alpha > 0) if name == "ssr" else alpha / (49 if name == "clustered" else 1)
    Image.fromarray(np.rint(np.clip(mask, 0, 1)*255).astype("uint8")).save(out / (name + "-mask.png"))
    exposed = 1 - np.exp(-4 * frame[:, :, :3])
    srgb = np.where(exposed < .0031308, 12.92*exposed, 1.055*np.maximum(exposed, 0)**(1/2.4)-.055)
    Image.fromarray(np.rint(np.clip(srgb, 0, 1)*255).astype("uint8")).save(out / (name + ".png"))

mask = np.fromfile(raw / "replay-cluster-mask.bin", dtype="<u4").reshape(-1, 2)
stats["clusters"] = int(mask.shape[0])
stats["clusters_containing_a_light_above_32"] = int(np.count_nonzero(mask[:, 1]))
assert not np.any(mask[:, 1] & np.uint32(0xfffe0000))
(out / "comparison.json").write_text(json.dumps(stats, indent=2) + "\n", encoding="utf-8")
print(json.dumps(stats, indent=2))
