"""Compare replay_material_studio.py exports with the mirror lighting switched off and on."""
import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "output/profiling/material-studio"


def read_view(directory, label):
    replay = json.loads((directory / "replay.json").read_text(encoding="utf8"))
    width, height = replay["size"]
    if replay["frame"] != 24 or (width, height) != (800, 513) or replay["messages"]:
        raise ValueError(f"Unexpected frame, size or Vulkan message: {directory}")
    frame = (directory / "frame-path.txt")
    capture = Path(frame.read_text(encoding="utf8")) if frame.exists() else ROOT / "output/captures/local" / replay["file"]
    with capture.open("rb") as source:
        digest = hashlib.file_digest(source, "sha256").hexdigest()

    def pixels(name):
        image = np.fromfile(directory / (name + ".f32"), dtype="<f4").reshape(height, width, 4)
        if not np.isfinite(image).all():
            raise ValueError(f"Invalid pixel in {label}/{name}")
        return image

    # display 已经过编辑器的显示变换；测量用转换前的 light，另存 PNG 供人对照。
    display = pixels("display")[..., :3]
    Image.fromarray(np.uint8(np.clip(display, 0, 1) * 255 + .5)).save(OUTPUT / (label + ".png"))
    # 中心 (524,220) 落在机身右侧的框边上，取周围 5 × 5 个像素降低单点噪声。
    region = pixels("light")[218:223, 522:527, :3]
    return {"capture": capture.relative_to(ROOT).as_posix(), "sha256": digest, "frame": replay["frame"],
            "pack_event": replay["pack_event"], "display_event": replay["display_event"],
            "mean_linear_rgb": region.mean(axis=(0, 1), dtype=np.float64).tolist()}, pixels("surface")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("off", type=Path)
    parser.add_argument("on", type=Path)
    parser.add_argument("--previous", type=Path, help="Optionally retain an earlier engine as a separate comparison")
    args = parser.parse_args()
    OUTPUT.mkdir(parents=True, exist_ok=True)
    off, a = read_view(args.off, "no-ibl")
    on, b = read_view(args.on, "after")
    if not np.array_equal(a, b):
        raise ValueError("The two captures have different material data or pixel coordinates.")
    report = {"size": [800, 513], "region_xywh": [522, 218, 5, 5], "specular_off": off, "specular_on": on}
    if args.previous:
        report["previous_engine"], _ = read_view(args.previous, "before")
    (OUTPUT / "measurements.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf8")
    print(f"Mean linear red at the camera frame: {off['mean_linear_rgb'][0]:.5f} -> {on['mean_linear_rgb'][0]:.5f}")
