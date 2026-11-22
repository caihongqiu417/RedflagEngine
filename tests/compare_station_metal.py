"""比较两版游戏的同机位画面，并在同一新版帧中只换反射着色器，分开记录两种变化。"""

import argparse
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path

import numpy as np
from capture_material_studio import run_renderdoc
from PIL import Image, ImageDraw, ImageFont
from profile_native_navigation import ROOT

STUDY = Path("metal-furnace/station")
OUT = ROOT / "output/validation" / STUDY
CROP = (624, 420, 1184, 832)
LUMA = np.array([0.2126, 0.7152, 0.0722])


def sha(path):
    with path.open("rb") as file:
        return hashlib.file_digest(file, "sha256").hexdigest()


def floats(path, channels):
    return np.memmap(path, dtype="<f4", mode="r", shape=(1080, 1920, channels))


def recorded(exe):
    executable = sha(exe)
    folder = executable[:12]
    report = json.loads((OUT / folder / "rack-current.json").read_bytes())
    work = ROOT / "tmp" / STUDY / folder / "rack-current"
    frame = OUT / folder / "rack-current.png"
    assert report["size"] == [1920, 1080] and report["frame"] == 12 and not report["messages"]
    z = [floats(work / (stage + "-depth.f32"), 1)[:, :, 0] for stage in ("before", "after", "final")]
    mask = (z[0] != z[1]) & (z[1] == z[2])
    image = np.asarray(Image.open(frame).convert("RGB"))
    scene = work / "assets/scenes/Watt-Station.redflag"
    info = {
        "executable_sha256": executable,
        "scene_sha256": sha(scene),
        "render_settings_sha256": sha(Path(str(scene) + ".render.yaml")),
        "rdc_sha256": sha(ROOT / report["capture"]),
        "png_sha256": sha(frame),
        "visible_rack_pixels": int(mask.sum()),
        "frame": report["frame"],
        "draw_event": report["material_draws"][0]["event"],
        "display": report["linear_light"],
        "capture": report["capture"],
    }
    return work, report, mask, image, info


def change(before, after, mask):
    old = before[mask, :3] @ LUMA
    new = after[mask, :3] @ LUMA
    delta = new - old
    return {
        "before": float(old.mean()),
        "after": float(new.mean()),
        "mean_change": float(delta.mean()),
        "percent_of_before": float(100 * delta.mean() / old.mean()),
        "median_change": float(np.median(delta)),
        "p95_change": float(np.percentile(delta, 95)),
    }


def crop(left, right, labels, name):
    x, y, r, b = CROP
    tiles = [Image.fromarray(image[y:b, x:r]) for image in (left, right)]
    width, height = tiles[0].size
    canvas = Image.new("RGB", (2 * width + 6, height + 36), (35, 37, 40))
    font = ImageFont.load_default(size=17)
    draw = ImageDraw.Draw(canvas)
    for i, tile in enumerate(tiles):
        canvas.paste(tile, (i * (width + 6), 36))
        draw.text((i * (width + 6) + 10, 7), labels[i], fill="#f5f5f5", font=font)
    canvas.save(OUT / name)


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument(
        "--before", type=Path, default=ROOT / "tmp/aa-studies/station-package-native/RedflagForge.exe"
    )
    cli.add_argument(
        "--after", type=Path, default=ROOT / "bin/Debug-windows-x86_64/RedflagForge/RedflagForge.exe"
    )
    cli.add_argument("--renderdoc", type=Path, default=ROOT / "tmp/tools/renderdoc/RenderDoc_1.47_64")
    cli.add_argument("--from-readbacks", action="store_true", help="沿用两局的 GPU 读回，重新统计")
    args = cli.parse_args()
    if not args.from_readbacks:
        for exe in (args.before, args.after):
            subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "tests/capture_station_materials.py"),
                    "rack",
                    "current",
                    "--linear",
                    "--study",
                    str(STUDY),
                    "--exe",
                    str(exe),
                    "--renderdoc",
                    str(args.renderdoc),
                ],
                check=True,
            )
        work = ROOT / "tmp" / STUDY / sha(args.after)[:12] / "rack-current"
        run_renderdoc(
            args.renderdoc.resolve(),
            ROOT / "tests/replay_station_metal.py",
            ROOT,
            dict(os.environ, RF_RACK_WORK=str(work), RF_RACK_OUT=str(OUT)),
        )

    w0, d0, m0, im0, old = recorded(args.before)
    w1, d1, m1, im1, new = recorded(args.after)
    match = m0 & m1
    assert (
        old["scene_sha256"] == new["scene_sha256"]
        and old["render_settings_sha256"] == new["render_settings_sha256"]
    )
    assert old["display"]["exposure_multiplier"] == new["display"]["exposure_multiplier"]
    assert d0["material_draws"][0]["clip_matrix"] == d1["material_draws"][0]["clip_matrix"]
    assert np.array_equal(floats(w0 / "albedo.f32", 4)[match], floats(w1 / "albedo.f32", 4)[match])
    hdr0, hdr1 = (floats(w / "light.f32", 4) for w in (w0, w1))
    native = {
        "before": old,
        "after": new,
        "common_rack_pixels": int(match.sum()),
        "different_visibility": int((m0 ^ m1).sum()),
        "identical_matrix": True,
        "identical_common_albedo": True,
        "linear_luminance": change(hdr0, hdr1, match),
        "display_luma_0_255": change(im0, im1, match),
    }
    crop(im0, im1, ("Previous build", "Updated build"), "build-crop.png")

    # 同一帧保持几何、相机、各项光照输入和 TAA 的前帧不变；检查替换前后的输入确实逐字节相同。
    ref = json.loads((OUT / "same-frame.json").read_bytes())
    raw = lambda version, kind: w1 / f"replay-{version}-{kind}.f32"
    assert sha(raw("single", "input")) == sha(raw("current", "input"))
    assert sha(raw("current", "light")) == sha(w1 / "light.f32")
    previous, actual = (
        np.asarray(Image.open(w1 / f"replay-{m}.png").convert("RGB")) for m in ("single", "current")
    )
    assert np.array_equal(actual, im1)
    isolated = {
        "same_input_bytes": True,
        "matches_recorded_color": True,
        "rack_pixels": int(m1.sum()),
        "event": ref["event"],
        "linear_luminance": change(floats(raw("single", "light"), 4), hdr1, m1),
        "display_luma_0_255": change(previous, actual, m1),
        "reflection_pass_luminance": change(
            floats(raw("single", "reflected"), 4), floats(raw("current", "reflected"), 4), m1
        ),
    }
    crop(previous, actual, ("Single scattering / same frame", "Compensated / same frame"), "shader-crop.png")
    results = {"builds": native, "shader_only": isolated, "crop_xyxy": CROP}
    print(json.dumps(results, indent=2))
    (OUT / "comparison.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf8")


if __name__ == "__main__":
    main()
