"""用同机位的 Vulkan 截帧比较天空背景、材质反射，以及关掉背景后的模型亮度。"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

import numpy as np
from PIL import Image
import yaml

from check_directional_viewport import ROOT


def capture(scene, exe, tools, digest, width):
    subprocess.run([sys.executable, str(ROOT / "tests/capture_material_studio.py"), "--exe", str(exe),
                    "--renderdoc", str(tools), "--scene", str(scene), "--width", str(width)], cwd=ROOT, check=True)
    return ROOT / "tmp/material-reference" / f"{scene.stem}-{digest[:12]}-{width}"


def read_frame(work, png):
    replay = json.loads((work / "replay.json").read_text())
    width, height = replay["size"]
    assert replay["frame"] == 24 and replay["pack_event"] and not replay["messages"], replay
    assert replay["shading_size"] == [width, height], "Use native shading for this comparison"

    def pixels(name):
        return np.fromfile(work / name, "<f4").reshape(height, width, 4)

    display = pixels("display.f32")[:, :, :3]
    light = pixels("light.f32")[:, :, :3]
    assert np.isfinite(display).all() and np.isfinite(light).all()
    image = np.uint8(np.clip(display, 0, 1) * 255 + .5)
    Image.fromarray(image).save(png)
    material = np.abs(pixels("surface.f32")[:, :, 3]).astype(np.uint32)
    capture_file = Path((work / "frame-path.txt").read_text(encoding="utf8"))
    log = (work / "editor.log").read_text(encoding="utf8", errors="replace").splitlines()
    gpu = next(line.split("Vulkan device: ", 1)[1] for line in log if "Vulkan device: " in line)
    scene_copy = work / "assets/scenes/Sponza-Atrium.redflag"
    info = dict(frame=24, size=[width, height], pack=replay["pack_event"],
                display=replay["display_event"], renderdoc=replay["tool"],
                tone_mapping=replay.get("tone_mapping", "Reinhard"),
                exposure_multiplier=replay.get("exposure_multiplier", 1.0),
                gpu=gpu, scene_sha256=hashlib.sha256(scene_copy.read_bytes()).hexdigest(),
                debug_messages=replay["messages"], capture=capture_file.relative_to(ROOT).as_posix())
    return image, light, material, info


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before-work", required=True, type=Path, help="Old default scene frame exported by capture_material_studio.py")
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--scene", type=Path, default=ROOT / "RedflagForge/assets/scenes/Sponza-Atrium.redflag")
    parser.add_argument("--out", type=Path, default=ROOT / "output/validation/atrium-sky")
    parser.add_argument("--renderdoc", type=Path, default=ROOT / "tmp/tools/renderdoc/RenderDoc_1.47_64")
    args = parser.parse_args()
    exe = args.exe.resolve(strict=True)
    with exe.open("rb") as binary:
        digest = hashlib.file_digest(binary, "sha256").hexdigest()
    scene = args.scene.resolve(strict=True)
    settings = yaml.safe_load(Path(str(scene) + ".render.yaml").read_text(encoding="utf8"))
    assert settings.get("SpecularIBL", True), "The default under test should enable specular IBL"

    # 保留原机位、灯、环境图和探针。每份对照场景只切换一个选项。
    temp = ROOT / "tmp/sky-atrium-review"
    temp.mkdir(parents=True, exist_ok=True)
    control = temp / (scene.stem + "-no-specular.redflag")
    hidden = temp / (scene.stem + "-hidden.redflag")
    shutil.copy2(scene, control)
    Path(str(control) + ".render.yaml").write_text(yaml.safe_dump(dict(settings, SpecularIBL=False)), encoding="utf8")
    data = yaml.safe_load(scene.read_text(encoding="utf8"))
    data["SkyVisible"] = False
    hidden.write_text(yaml.safe_dump(data, allow_unicode=True), encoding="utf8")
    shutil.copy2(str(scene) + ".render.yaml", str(hidden) + ".render.yaml")
    probes = Path(str(scene) + ".rfprobes")
    for variant in (control, hidden):
        variant_probes = Path(str(variant) + ".rfprobes")
        if probes.is_file():
            shutil.copy2(probes, variant_probes)
        else:
            variant_probes.unlink(missing_ok=True)

    before = args.before_work.resolve()
    width = json.loads((before / "replay.json").read_text())["size"][0]
    tools = args.renderdoc.resolve()
    work = {"before": before}
    for name, variant in (("sky-no-specular", control), ("after", scene), ("hidden", hidden)):
        work[name] = capture(variant, exe, tools, digest, width)
    compare(work, args.out, scene, digest, dict(settings, PreviewWidth=width))


def interior(mask):
    # 去掉离模型轮廓两像素以内的点，避免混入 TAA 留下的边缘颜色。
    neighborhoods = np.lib.stride_tricks.sliding_window_view(np.pad(mask, 2), (5, 5))
    return neighborhoods.all(axis=(-2, -1))


def compare(work, out, scene, digest, settings):
    out.mkdir(parents=True, exist_ok=True)
    images, lights, surfaces, frames = {}, {}, {}, {}
    for name, directory in work.items():
        images[name], lights[name], surfaces[name], frames[name] = read_frame(directory, out / (name + ".png"))
    assert all(image.shape == images["before"].shape for image in images.values()), "Compare the same camera and resolution"
    for name in ("sky-no-specular", "hidden"):
        assert np.array_equal(surfaces[name], surfaces["after"]), "The switches should preserve geometry and material"

    sky = interior(np.logical_and.reduce([surface == 0 for surface in surfaces.values()]))
    assert sky.any(), "The camera must see some sky away from geometry edges"
    assert images["after"][sky].any(), "The sky is black throughout the visible background"
    assert np.array_equal(images["sky-no-specular"][sky], images["after"][sky]), "The visible sky changed with specular IBL"
    assert not images["hidden"][sky].any(), "Hiding the sky should uncover the black background"

    # 材质高 12 位保存金属度。单独看金属度超过一半的像素，避免地面占满统计。
    surface = surfaces["after"]
    geometry = surface != 0
    metal = np.zeros_like(geometry)
    metal[geometry] = ((surface[geometry] - 1) >> 12) > 2047
    report = dict(new_executable_sha256=digest, scene=scene.name,
                  capture_settings=settings, frames=frames,
                  sky_pixels=int(sky.sum()), sky_excluded_border_px=2,
                  sky_rgb8_mean={name: im[sky].mean(axis=0).tolist() for name, im in images.items()},
                  sky_unique_rgb8=int(len(np.unique(images["after"][sky], axis=0))))
    delta = (lights["after"].astype(np.float64) - lights["sky-no-specular"]) @ [0.2126, 0.7152, 0.0722]
    for name, mask in (("geometry", geometry), ("mostly_metal", metal)):
        count = int(mask.sum())
        report[name] = dict(pixels=count)
        if count:
            report[name].update(mean_linear_luminance_gain=float(delta[mask].mean()),
                                pixels_brighter_over_0_001=int((delta[mask] > .001).sum()))
    assert report["geometry"]["pixels_brighter_over_0_001"] > 0, "No measured specular IBL on the model"
    solid = interior(geometry)
    assert solid.any()
    error = float(np.abs(lights["hidden"][solid] - lights["after"][solid]).max())
    report["hide_sky"] = dict(interior_geometry_pixels=int(solid.sum()), max_abs_linear_rgb_difference=error,
                              mean_linear_luminance_gain_vs_no_specular=float(delta[solid].mean()))
    assert error == 0, "Hiding the sky changed the lighting inside the model silhouette"
    (out / "comparison.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf8")
    print(out)


if __name__ == "__main__":
    main()
