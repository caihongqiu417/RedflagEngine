"""Compare the bunny silhouette in the four RenderDoc PNGs from replay_mesh_lod.py."""
from pathlib import Path
import json

import numpy as np
from PIL import Image

out = Path(__file__).resolve().parents[1] / "output/profiling/mesh-lod"
result = {}
for api in ("OpenGL", "Vulkan"):
    masks = []
    for mode in ("full", "auto"):
        rgb = np.asarray(Image.open(out / f"{api}-{mode}.png").convert("RGB"), dtype=int)
        # 兔子是棕色，地板是蓝灰色，两种后端的背景都是中性灰。
        masks.append(rgb[:, :, 0] > rgb[:, :, 2] + 20)
    full, reduced = masks
    union = (full | reduced).sum()
    overlap = (full & reduced).sum()
    assert union and overlap, f"{api}: rabbit is missing"
    result[api] = dict(full_pixels=int(full.sum()), reduced_pixels=int(reduced.sum()),
                       difference_pixels=int(union - overlap), intersection_over_union=float(overlap / union))
(out / "silhouette.json").write_text(json.dumps(result, indent=2) + "\n")
