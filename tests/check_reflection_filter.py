"""用 RTXGI Bathroom 的同一帧测反射降噪，并从截帧输入重新计算 256 次采样的参考图。"""
import argparse
import json
import os
from pathlib import Path
import shutil
from statistics import median
import subprocess
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont
import yaml

from capture_material_studio import run_renderdoc
from check_scene_reflections import digest, ROOT, SCENES, WORK


OUTPUT = ROOT / "output/profiling/rough-reflections"
BIN = ROOT / "bin/Debug-windows-x86_64/RedflagForge"


def image(work, name, size, channels=4):
    w, h = size
    data = np.fromfile(work / (name + ".f32"), "<f4").reshape(h, w, channels)
    if not np.isfinite(data).all():
        raise ValueError(f"Invalid colors in {work / name}")
    return data


def record(args, name, width, enabled, stamp):
    source = SCENES / "RTXGI-Bathroom.redflag"
    scene = WORK / "rough-scenes" / ("rough-" + name + ".redflag")
    scene.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, scene)
    shutil.copyfile(str(source) + ".rfprobes", str(scene) + ".rfprobes")
    settings = yaml.safe_load(Path(str(source) + ".render.yaml").read_bytes())
    settings.update(SSR=True, SceneReflections=True, TAA=True, DenoiseReflections=enabled, PreviewWidth=width)
    Path(str(scene) + ".render.yaml").write_text(yaml.safe_dump(settings), encoding="utf8")
    env = dict(os.environ, RF_MEASURE_REFLECTIONS="1")
    if name == "small-off":
        env["RF_EXPORT_REFLECTION_SCENE"] = "1"
    work = WORK / f"{scene.stem}-{stamp[:12]}-{width}"
    if args.replay:
        path = Path((work / "frame-path.txt").read_text(encoding="utf8"))
        env.update(RF_STUDIO_CAPTURE=str(path), RF_STUDIO_EXPORT=str(work))
        run_renderdoc(args.renderdoc, ROOT / "tests/replay_material_studio.py", ROOT, env)
    else:
        subprocess.run([sys.executable, str(ROOT / "tests/capture_material_studio.py"), "--exe", str(args.exe),
                        "--renderdoc", str(args.renderdoc), "--width", str(width), "--scene", str(scene)],
                       cwd=ROOT, env=env, check=True)
    info = json.loads((work / "replay.json").read_bytes())
    assert info["frame"] == 24 and not info["messages"]
    entry = work / "assets/scenes/Sponza-Atrium.redflag"
    assert entry.read_bytes() == source.read_bytes()
    assert yaml.safe_load(Path(str(entry) + ".render.yaml").read_bytes()) == settings
    assert digest(Path(str(entry) + ".rfprobes")) == digest(Path(str(source) + ".rfprobes"))
    trace = info["reflection"]
    assert trace["scene_trace"] and ("filter" in trace) == enabled
    timings = trace["gpu_ms"]
    w, h = info["size"]
    if enabled:
        filtered = trace["filter"]
        assert trace["event"] < filtered["event"] < info["display_event"] and len(filtered["gpu_ms"]) == 11
        timings = [a + b for a, b in zip(timings, filtered["gpu_ms"])]
    assert len(timings) == 11
    path = Path((work / "frame-path.txt").read_text(encoding="utf8"))
    info.update(capture=path.relative_to(ROOT).as_posix(), capture_sha256=digest(path),
                median_reflections_ms=median(timings), scratch_bytes=w*h*16 if enabled else 0,
                settings_sha256=digest(Path(str(entry) + ".render.yaml")))
    display = image(work, "display", info["size"])[:, :, :3]
    Image.fromarray(np.uint8(np.clip(display, 0, 1)*255+.5)).save(OUTPUT / (name + ".png"))
    return work, info


def mse(x, y):
    return float(np.square(x - y).mean(dtype=np.float64))


def build_reference(work, size, tool):
    subprocess.run([str(tool), str(work), "--frame-reference"], cwd=ROOT, check=True,
                   env=dict(os.environ, PATH=str(BIN)+os.pathsep+os.environ["PATH"], RF_VULKAN_VALIDATION="1"))
    # 奇数和偶数帧各算 128 次。比较这两张平均图，可以看出 256 次采样得到的参照还有多少噪声。
    a, b = [image(work, "reference-" + str(i), size, 3).astype(np.float64) for i in range(2)]
    return (a + b) * .5, mse(a, b)


def reference_figure(raw, reference, report, path):
    w, h = report["size"]
    # 在原尺寸线性 RGB 中找出平均误差最大的一块，只用来选放大位置。报告的 MSE 仍取全图。
    width, height = max(1, w // 3), max(1, h // 4)
    error = np.square(raw - reference).mean(axis=2)
    integral = np.pad(error.cumsum(0).cumsum(1), ((1, 0), (1, 0)))
    scores = integral[height:, width:] - integral[:-height, width:] - integral[height:, :-width] + integral[:-height, :-width]
    y, x = map(int, np.unravel_index(np.argmax(scores), scores.shape))
    box = (x, y, x + width, y + height)
    column = 440
    full_height, zoom_height = round(column * h / w), round(column * height / width)
    zoom_top = 95 + full_height
    sheet = Image.new("RGB", (2 * column + 60, zoom_top + zoom_height + 59), "#f7f6f2")
    draw = ImageDraw.Draw(sheet)
    title = ImageFont.truetype("msyhbd.ttc", 18)
    font = ImageFont.truetype("msyh.ttc", 15)
    for left, rgb, label in ((20, raw, "截帧画面 · 每像素 1 次采样"), (40 + column, reference, "参考画面 · 每像素 256 次采样")):
        light = np.maximum(rgb, 0)
        picture = Image.fromarray(np.uint8((light / (1 + light)) ** (1 / 2.2) * 255 + .5))
        draw.text((left, 14), label, fill="#203639", font=title)
        sheet.paste(picture.resize((column, full_height), Image.Resampling.LANCZOS), (left, 52))
        outline = (left + round(x * column / w), 52 + round(y * full_height / h),
                   left + round((x + width) * column / w), 52 + round((y + height) * full_height / h))
        draw.rectangle(outline, outline="#ffe3a0", width=2)
        draw.text((left, zoom_top - 29), "框内局部", fill="#445457", font=font)
        sheet.paste(picture.crop(box).resize((column, zoom_height), Image.Resampling.NEAREST), (left, zoom_top))
    draw.text((20, zoom_top + zoom_height + 15), f"场景反射 · 第 {report['frame']} 帧 · {w} × {h} 像素 · 两图使用相同的显示亮度曲线",
              fill="#445457", font=font)
    sheet.save(path)
    return list(box)


def check_existing_capture(directory, renderdoc, tool):
    path = Path((directory / "frame-path.txt").read_text(encoding="utf8"))
    work = directory / "reference-check"
    result = work / "replay.json"
    result.unlink(missing_ok=True)
    env = dict(os.environ, RF_STUDIO_CAPTURE=str(path), RF_STUDIO_EXPORT=str(work),
               RF_MEASURE_REFLECTIONS="1", RF_EXPORT_REFLECTION_SCENE="1")
    run_renderdoc(renderdoc, ROOT / "tests/replay_material_studio.py", ROOT, env)
    info = json.loads(result.read_bytes())
    if "error" in info:
        raise RuntimeError(info["error"])
    assert info["reflection"]["scene_trace"] and not info["messages"]
    size = info["size"]
    reference, half_error = build_reference(work, size, tool)
    raw = image(work, "reflection-out", size)
    frame_data = work / "reflection-buffers/0.bin"
    eye = np.fromfile(frame_data, "<f4", count=3, offset=5 * 64).tolist()
    seed = int(np.fromfile(frame_data, "<u4", count=1, offset=5 * 64 + 16 + 8)[0])
    report = dict(capture_sha256=digest(path), reference_executable_sha256=digest(tool), size=size, camera_eye=eye,
                  frame=info["frame"], event=info["reflection"]["event"],
                  alpha_mask=info["reflection"].get("alpha_mask", False), single_sample_reproduced=True,
                  samples=256, reference_seeds=[seed + 1, seed + 256],
                  mse_raw=mse(raw[:, :, :3], reference), half_reference_mse=half_error)
    if "filter" in info["reflection"]:
        filtered = image(work, "reflection-filtered", size)
        assert np.array_equal(raw[:, :, 3], filtered[:, :, 3])
        report["mse_filtered"] = mse(filtered[:, :, :3], reference)
    figure = work / "comparison.png"
    report["detail_box"] = reference_figure(raw[:, :, :3], reference, report, figure)
    report["figure_sha256"] = digest(figure)
    report["raw_sha256"] = digest(work / "reflection-out.f32")
    report["half_reference_sha256"] = [digest(work / f"reference-{i}.f32") for i in range(2)]
    (work / "reference.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf8")
    print(json.dumps(report, indent=2))


def compare(off, on, size, tool):
    # 两次截帧的原始反射和底图必须相同。截帧里保存的场景数据只在算参考图时使用。
    for name in ("reflection-in.f32", "reflection-out.f32", "surface.f32"):
        assert digest(off / name) == digest(on / name), name
    reference, half_error = build_reference(off, size, tool)
    raw = image(off, "reflection-out", size)
    filtered = image(on, "reflection-filtered", size)
    assert np.array_equal(raw[:, :, 3], filtered[:, :, 3]), "Filtering changed the ray hit flags"
    raw_mse, filtered_mse = [mse(v[:, :, :3], reference) for v in (raw, filtered)]
    assert filtered_mse < raw_mse, (raw_mse, filtered_mse)
    # 用面板着色器的显示曲线导出 TAA 之前的同一块画面，保留原像素，不再锐化。
    def show(v):
        positive = np.maximum(v, 0)
        return Image.fromarray(np.uint8(np.clip((positive/(positive+1))**(1/2.2), 0, 1)*255+.5))
    w, h = size
    box = (int(.50*w), int(.15*h), int(.84*w), int(.54*h))
    for name, data in (("off", raw[:, :, :3]), ("on", filtered[:, :, :3]), ("reference", reference)):
        show(data).crop(box).save(OUTPUT / ("detail-" + name + ".png"))
    return dict(samples=256, size=size, pixels=w*h, detail_box=box, mse_raw=raw_mse,
                mse_filtered=filtered_mse, half_reference_mse=half_error,
                mean_rgb_raw=raw[:, :, :3].mean(axis=(0,1),dtype=np.float64).tolist(),
                mean_rgb_filtered=filtered[:, :, :3].mean(axis=(0,1),dtype=np.float64).tolist(),
                mean_rgb_reference=reference.mean(axis=(0,1)).tolist(), single_sample_reproduced=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=BIN / "RedflagForge.exe")
    parser.add_argument("--reference-exe", type=Path, default=ROOT / "bin-int/Tests/EnvironmentLightingTests.exe")
    parser.add_argument("--renderdoc", type=Path, default=ROOT / "tmp/tools/renderdoc/RenderDoc_1.47_64")
    parser.add_argument("--replay", action="store_true", help="Measure the existing captures again")
    parser.add_argument("--capture-dir", type=Path, help="从此目录的 frame-path.txt 计算单张场景反射的参考图")
    args = parser.parse_args()
    if args.capture_dir:
        check_existing_capture(args.capture_dir.resolve(strict=True), args.renderdoc.resolve(strict=True),
                               args.reference_exe.resolve(strict=True))
        return
    args.exe = args.exe.resolve(strict=True)
    args.renderdoc = args.renderdoc.resolve(strict=True)
    stamp = digest(args.exe)
    OUTPUT.mkdir(parents=True, exist_ok=True)
    cases, directories = {}, {}
    for name, width in (("small", 640), ("native", 0)):
        for enabled in (False, True):
            label = name + ("-on" if enabled else "-off")
            directories[label], cases[label] = record(args, label, width, enabled, stamp)
        for file in ("reflection-in.f32", "reflection-out.f32"):
            assert digest(directories[name + "-off"]/file) == digest(directories[name + "-on"]/file)
    assert cases["native-on"]["size"][0] > cases["small-on"]["size"][0]
    metrics = compare(directories["small-off"], directories["small-on"], cases["small-off"]["size"],
                      args.reference_exe.resolve(strict=True))
    report = dict(executable_sha256=stamp, reference_executable_sha256=digest(args.reference_exe),
                  scene="RTXGI-Bathroom", cases=cases, metrics=metrics)
    (OUTPUT / "bathroom.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf8")
    print(json.dumps(metrics, indent=2))


if __name__ == "__main__":
    main()
