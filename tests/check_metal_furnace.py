"""在均匀的白色环境光下比较 Redflag 两个后端的金属反射，保存实际着色前后的线性读数。"""

import argparse
import csv
import json
import os
import re
import shutil
import struct
import subprocess
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
BASE = "18a014d"
TILE = 65
WORK = ROOT / "bin-int/Tests/metal-furnace"
OUT = ROOT / "output/validation/metal-furnace"


def shaders():
    gl = "RedflagForge/assets/shaders/Renderer3D_Mesh.glsl"
    vk = "Redflag/src/Redflag/Renderer/ScreenSpace/Shaders/StochasticSSR.comp.inc"
    for path in (gl, vk):
        old = subprocess.check_output(["git", "show", BASE + ":" + path], cwd=ROOT)
        for label, data in (("before", old), ("after", (ROOT / path).read_bytes())):
            if path == gl:
                (WORK / (label + ".glsl")).write_bytes(data)
            else:
                words = [int(w, 16) for w in re.findall(rb"0x[0-9a-fA-F]+", data)]
                (WORK / (label + ".spv")).write_bytes(struct.pack(f"<{len(words)}I", *words))


def main():
    WORK.mkdir(parents=True, exist_ok=True)
    OUT.mkdir(parents=True, exist_ok=True)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--renderdoc", type=Path, help="capture the corrected Vulkan dispatch with renderdoc.dll"
    )
    args = parser.parse_args()
    shaders()
    fixture = ROOT / "tests/run_environment_lighting.cmd"
    command = [str(fixture), str(WORK)]
    if args.renderdoc:
        command.append(str(args.renderdoc.resolve(strict=True)))
    subprocess.run(command, cwd=ROOT, env={**os.environ, "RF_METAL_FURNACE": "1"}, check=True)
    if args.renderdoc:
        latest = max(WORK.glob("Furnace*.rdc"), key=lambda path: path.stat().st_mtime)
        shutil.copyfile(latest, OUT / "furnace.rdc")
    frames = {}
    yy, xx = np.indices((TILE * 3, TILE * 4))
    inside = ((yy % TILE - TILE // 2) ** 2 + (xx % TILE - TILE // 2) ** 2) < (TILE // 2) ** 2
    rows = []
    for backend in ("opengl", "vulkan"):
        for label in ("before", "after", "black"):
            image = np.fromfile(WORK / f"{backend}-{label}.rgba", "<f4").reshape(TILE * 3, TILE * 4, 4)[
                :, :, :3
            ]
            assert np.isfinite(image).all()
            frames[backend, label] = image
            for row, material in enumerate(("white_metal", "colored_metal", "black_plastic")):
                for col, roughness in enumerate((0.1, 0.4, 0.7, 1.0)):
                    for dx in (0, 24, 31):
                        pixel = image[row * TILE + TILE // 2, col * TILE + TILE // 2 + dx]
                        rows.append(
                            [
                                backend,
                                label,
                                material,
                                roughness,
                                np.sqrt(1 - (dx / (TILE // 2)) ** 2),
                                *pixel,
                            ]
                        )
    with (OUT / "linear.csv").open("w", newline="", encoding="utf8") as out:
        writer = csv.writer(out)
        writer.writerow(
            ["backend", "shader", "material", "roughness", "NdotV", "linear_r", "linear_g", "linear_b"]
        )
        writer.writerows(rows)
    metrics = {}
    for backend in ("opengl", "vulkan"):
        old, new, black = [frames[backend, mode] for mode in ("before", "after", "black")]
        ideal = inside & (yy < TILE)
        plastic = inside & (yy >= 2 * TILE)
        tinted = inside & (yy >= TILE) & (yy < 2 * TILE)
        center = (TILE // 2, 3 * TILE + TILE // 2)
        metrics[backend] = {
            "rough_white_center_before": float(old[center][0]),
            "rough_white_center_after": float(new[center][0]),
            "max_white_abs_error": float(np.max(np.abs(new[ideal] - 1))),
            "max_dielectric_change": float(np.max(np.abs(new[plastic] - old[plastic]))),
            "max_black_environment_output": float(np.max(np.abs(black[inside]))),
        }
        assert old[center][0] < 0.4 and metrics[backend]["max_white_abs_error"] < 0.002
        assert (
            metrics[backend]["max_dielectric_change"] < 0.0001
            and metrics[backend]["max_black_environment_output"] < 0.0001
        )
        assert np.all(new[tinted, 0] >= new[tinted, 1]) and np.all(new[tinted, 1] >= new[tinted, 2])
        assert np.max(new[tinted]) <= 1.001
        # PNG 仅做线性 RGB 到 sRGB 的传输变换；检查使用转换前的 float。
        pair = np.concatenate([old, new], axis=1).clip(0, 1)
        srgb = np.where(pair <= 0.0031308, 12.92 * pair, 1.055 * pair ** (1 / 2.4) - 0.055)
        Image.fromarray(np.rint(255 * srgb).astype("uint8")).save(OUT / f"{backend}.png")
    difference = frames["opengl", "after"] - frames["vulkan", "after"]
    metrics["backend_max_abs_error"] = float(np.max(np.abs(difference[inside])))
    assert metrics["backend_max_abs_error"] < 0.002
    print(json.dumps(metrics, indent=2))
    (OUT / "metrics.json").write_text(json.dumps(metrics, indent=2) + "\n", encoding="utf8")


if __name__ == "__main__":
    main()
