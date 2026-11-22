"""从仓库根目录运行 tests/run_mask_supersampling.cmd，结果写入 output/validation/mask-supersampling-current。"""
import csv
import hashlib
import json
import os
from pathlib import Path
import subprocess

import numpy as np
from PIL import Image, ImageDraw, ImageFont

from check_fine_mask import prepare

ROOT = Path(__file__).resolve().parents[1]
BASELINE = ROOT / "output/validation/fine-mask-motion"
ARCHIVE = ROOT / "output/validation/mask-supersampling"
OUT = ROOT / "output/validation/mask-supersampling-current"
WORK = ROOT / "tmp/mask-supersampling"
SIZE = 64
PALETTES = np.array([[[4, .125, .25], [.125, .375, 1.5]], [[.0625, .75, 2], [2, 1, .0625]]])


def digest(path):
    with path.open("rb") as file:
        return hashlib.file_digest(file, "sha256").hexdigest()


def cameras():
    source = BASELINE / "frames.csv"
    with source.open() as file:
        rows = list(csv.DictReader(file))
    p00 = 1024 * float(rows[0]["eye_z"]) / (SIZE * float(rows[0]["texels_per_pixel"]))
    for width in (8., 26.5):
        z = np.float32(SIZE * width * p00 / 1024)
        for i in range(65):
            x = np.float32((.375 + i/16) / 512)
            rows.append(dict(track=f"far{width:g}", frame=i, eye_x=float(x), eye_z=float(z),
                             texels_per_pixel=float(z)*1024/(SIZE*p00)))
    return rows


def record():
    rows = cameras()
    folder = prepare(WORK, False, True)
    (folder / "camera.txt").write_text("".join(f"{r['eye_x']} {r['eye_z']}\n" for r in rows), encoding="ascii")
    compiler = Path(os.environ["VULKAN_SDK"]) / "Bin/glslc.exe"
    subprocess.run([compiler, ROOT / "tests/fixtures/MaskLinearResolve.comp", "-o", folder / "resolve.spv"], check=True)
    env = dict(os.environ, RF_VULKAN_VALIDATION="1")
    env["PATH"] = str(ROOT / "bin/Debug-windows-x86_64/RedflagForge") + os.pathsep + env["PATH"]
    executables = [ROOT / ("bin-int/Tests/" + name + ".exe") for name in ("MaskSupersamplingBaseline", "MaskSupersampling")]
    for exe in executables:
        with (WORK / (exe.stem + ".log")).open("wb") as log:
            subprocess.run([str(exe), str(folder)], env=env, cwd=ROOT, stdout=log, stderr=log, check=True)
    source_files = [BASELINE / "frames.csv", BASELINE / "pixels.npz", folder / "mask.png", *executables, folder / "resolve.spv",
                    ROOT / "RedflagForge/src/Shaders/LightingPreview.comp.inc"]
    current = ROOT / "bin/Debug-windows-x86_64/Redflag/Redflag.lib"
    off = Path(os.environ.get("RF_MASK_BASELINE_LIB", current))
    libraries = {mode: dict(path=os.path.relpath(path, ROOT), sha256=digest(path)) for mode,path in
                 (("ssaa_off", off), ("ssaa_on", current))}
    return folder, rows, {path.name: digest(path) for path in source_files}, libraries


def frame_rgb(path, count):
    rgba = np.memmap(path, dtype="<f4", mode="r", shape=(count, SIZE, SIZE, 4))
    assert np.all(rgba[..., 3] == 1)
    return rgba[..., :3]


def verify_raw(folder, label, scale, palette, count, rendered):
    front, back = PALETTES[palette]
    for i in (0, 69, count-1):
        size = SIZE * scale
        raw = np.fromfile(folder / f"{label}-{palette}-raw-{i}.f32", "<f4").reshape(size, size, 4)[:, :, :3]
        assert np.all(np.all(raw == front, axis=2) | np.all(raw == back, axis=2))
        box = raw.reshape(SIZE, scale, SIZE, scale, 3).mean(axis=(1, 3))
        assert np.array_equal(box, rendered[i]), (label, palette, i)


def panel_rgb(linear):
    light = np.maximum(linear, 0)
    return np.power(light / (1+light), 1/2.2)


def picture(rgb):
    return Image.fromarray(np.uint8(np.clip(panel_rgb(rgb), 0, 1)*255 + .5))


def contact(tracks, center_rows, ref):
    side = 180
    width = side * 5 + 58
    canvas = Image.new("RGB", (width, 805), "#fbfbf7")
    font = ImageFont.truetype("msyh.ttc", 16)
    title = ImageFont.truetype("msyh.ttc", 21)
    draw = ImageDraw.Draw(canvas)
    draw.text((14, 9), "远处铁丝的实际线性颜色", fill="#20383b", font=title)
    draw.text((14, 44), "每行取一帧实际绘制的中央 64 个像素，时间向下。各图统一用编辑器的显示公式转换线性 RGB。", fill="#344046", font=font)
    fg, bg = PALETTES[0]
    panels = [(name, center_rows[key + "_palette0"]) for name, key in
              (("原生", "1"), ("旧 2×2", "2"), ("新版 2×2", "production-2"), ("旧 8×8", "8"))]
    panels.append(("面积参考", ref[..., None]*fg + (1-ref[..., None])*bg))
    labels = {"zoom": "相机后退，1 到 4 纹素 / 像素", "pan": "相机侧移，1.5 纹素 / 像素",
              "far8": "相机侧移，8 纹素 / 像素", "far26.5": "相机侧移，26.5 纹素 / 像素"}
    for band, name in enumerate(labels):
        pos = 80 + band * 180
        draw.text((14, pos), labels[name], fill="#233833", font=font)
        for col, (label, grid) in enumerate(panels):
            image = picture(grid[tracks == name])
            x = 14 + col*(side+7)
            canvas.paste(image.resize((side, 110), Image.Resampling.NEAREST), (x, pos+29))
            draw.text((x, pos+141), label, fill="#344046", font=font)
    canvas.save(OUT / "motion.png")


def full_frames(folder, rows, ref):
    font = ImageFont.truetype("msyh.ttc", 17)
    canvas = Image.new("RGB", (640, 490), "#fbfbf7")
    draw = ImageDraw.Draw(canvas)
    n = next(i for i,r in enumerate(rows) if r["track"] == "far26.5" and int(r["frame"]) == 32)
    draw.text((12, 8), "同一块远处的铁丝网，两种前后景颜色", font=font, fill="#243b40")
    for j,label in enumerate(("原生绘制", "新版 2×2", "面积参考")):
        draw.text((14 + j*210, 40), label, font=font, fill="#243b40")
    for p,(front,back) in enumerate(PALETTES):
        a = ref[n, ..., None]
        images = [frame_rgb(folder / f"{key}-{p}.f32", len(rows))[n] for key in ("1","production-2")]
        for j,rgb in enumerate([*images, a*front+(1-a)*back]):
            canvas.paste(picture(rgb).resize((200,200), Image.Resampling.NEAREST), (12+j*210,75+p*205))
    canvas.save(OUT / "two-colors.png")


def wire_area(rows):
    def solid(t):
        t = np.clip(t, 0, 1024)
        return np.floor(t/4) + np.minimum(t % 4, 1)
    half = np.arange(SIZE + 1) - SIZE / 2
    result = []
    for r in rows:
        width = float(r["texels_per_pixel"])
        x = 512 + 512*float(r["eye_x"]) + half * width
        y = 512 + half * width
        # 远到能看到方片边缘时，横向求面积只数方片内的铁丝，纵向还要乘方片在像素里占的高度。
        result.append((np.diff(np.clip(y, 0, 1024))/width)[:,None] * np.diff(solid(x))/width)
    return np.array(result)


def analyze(folder, rows, hashes, libraries):
    baseline = np.load(BASELINE / "pixels.npz")
    ref = wire_area(rows)
    n_archived = len(baseline["reference"])
    assert np.max(np.abs(ref[:n_archived,SIZE//2] - baseline["reference"])) < 1e-7
    tracks = np.array([r["track"] for r in rows])
    report = dict(device=(folder / "gpu.txt").read_text().splitlines()[0], sha256=hashes, libraries=libraries, frames=len(rows),
                  viewport=[SIZE, SIZE], linear_foreground_background=PALETTES.tolist(),
                  method="Independent GPU test. For each resolution, draw both depth-tested emissive surfaces in Vulkan, "
                         "resolve linear RGB in a compute shader, then read every pixel back. No TAA, tone mapping or gamma in the measured values. "
                         "Reference is exact integrated wire area times the two constant linear colors. Each stripe repeats every four source texels.",
                  tracks={})
    center_rows, records = {}, []
    for label in ("1", "2", "4", "8", "production-2"):
        scale = int(label.split("-")[-1])
        by_palette = []
        colors = []
        for palette, (front, back) in enumerate(PALETTES):
            rendered = frame_rgb(folder / f"{label}-{palette}.f32", len(rows))
            verify_raw(folder, label, scale, palette, len(rows), rendered)
            colors.append(rendered)
            center_rows[f"{label}_palette{palette}"] = np.array(rendered[:, SIZE//2])
            weights = (rendered - back) / (front - back)
            assert np.array_equal(weights, np.broadcast_to(weights[..., :1], weights.shape))
            by_palette.append(weights[..., 0])
            if label == "production-2":
                display = frame_rgb(folder / f"{label}-{palette}-panel.f32", len(rows))
                after = panel_rgb(rendered)
                before = weights[..., :1] * panel_rgb(front) + (1 - weights[..., :1]) * panel_rgb(back)
                err = float(np.abs(display - after).max())
                wrong = float(np.abs(display - before).max())
                assert err < 1e-5 and wrong > .05, (err, wrong)
                report.setdefault("display_shader", []).append(dict(palette=palette,
                    max_abs_error_vs_tonemap_after_resolve=err, max_abs_error_vs_tonemap_before_resolve=wrong))
        assert np.array_equal(*by_palette)
        a = by_palette[0]
        if label == "1":
            assert np.array_equal(a[:n_archived], baseline["coverage"]), "1× output changed from archived camera run"
        for name in ("fixed", "zoom", "pan", "far8", "far26.5"):
            select = tracks == name
            measured, expected = a[select], ref[select]
            error = measured - expected
            temporal = np.diff(error, axis=0)
            data = dict(frames=int(select.sum()), coverage_mae=float(np.abs(error).mean()),
                        peak_pixel_error=float(np.max(np.abs(error))),
                        temporal_mae=float(np.abs(temporal).mean()),
                        peak_area_error=float(np.abs(error.mean(axis=(1,2))).max()),
                        center_32px_wire_area=float(measured[:,16:48,16:48].mean()),
                        center_32px_reference=float(expected[:,16:48,16:48].mean()),
                        trailing_missing_frames=int(np.logical_and.accumulate((measured.sum(axis=(1,2)) == 0)[::-1]).sum()),
                        linear_rgb_mse=[], linear_rgb_temporal_mae=[])
            for palette, (front, back) in enumerate(PALETTES):
                rgb = colors[palette][select]
                diff = rgb - (expected[..., None]*front + (1-expected[..., None])*back)
                data["linear_rgb_mse"].append(float(np.square(diff).mean()))
                data["linear_rgb_temporal_mae"].append(float(np.abs(np.diff(diff, axis=0)).mean()))
            report["tracks"].setdefault(name, {})[label] = data
            for n, (aa, bb) in enumerate(zip(measured, expected)):
                records.append([label,name,n,aa.mean(),bb.mean(),np.abs(aa-bb).mean(),
                                np.abs(temporal[n-1]).mean() if n else ""])
            if name == "fixed": assert data["temporal_mae"] == 0
        # 根据这一维条纹在像素内的长度，N 个样本量化约到 1/N；缩小纹理时还可能丢掉铁丝。
        if scale >= 4:
            assert report["tracks"]["zoom"][label]["trailing_missing_frames"] == 0
            assert np.max(np.abs(a[:n_archived]-ref[:n_archived])) < 1/scale + 1e-5
    for name in ("zoom", "pan"):
        group = report["tracks"][name]
        assert group["8"]["coverage_mae"] < group["4"]["coverage_mae"] < group["1"]["coverage_mae"]
        assert group["8"]["temporal_mae"] < group["1"]["temporal_mae"]
        assert group["production-2"]["coverage_mae"] < group["1"]["coverage_mae"]
    for name in ("far8", "far26.5"):
        group = report["tracks"][name]
        assert group["1"]["trailing_missing_frames"] == 65 and group["production-2"]["trailing_missing_frames"] == 0
        measured = group["production-2"]["center_32px_wire_area"]
        assert abs(measured - group["production-2"]["center_32px_reference"]) < .005
    OUT.mkdir(parents=True, exist_ok=True)
    archive = ARCHIVE / "linear-center-rows.npz"
    if archive.is_file() and archive.parent != OUT:
        with np.load(archive) as old:
            off = [key for key in center_rows if not key.startswith("production")]
            assert all(np.array_equal(center_rows[key],old[key]) for key in off)
        report["archived_off_check"] = dict(file_sha256=digest(archive), matched_sequences=off,
                                            scope="All center-row RGB values in the 325 frames, both palettes, four scales.")
    (OUT / "report.json").write_text(json.dumps(report, indent=2)+"\n", encoding="utf8")
    with (OUT / "frames.csv").open("w", newline="", encoding="utf8") as file:
        writer = csv.writer(file)
        writer.writerow(["scale_per_axis","track","frame","mean_wire_area","reference","area_mae_per_pixel","frame_delta_mae_per_pixel"])
        writer.writerows(records)
    np.savez_compressed(OUT / "linear-center-rows.npz", reference=ref[:,SIZE//2], **center_rows)
    contact(tracks, center_rows, ref[:,SIZE//2])
    full_frames(folder, rows, ref)
    for name, items in report["tracks"].items():
        print(name, [(s, round(d["coverage_mae"],5), round(d["temporal_mae"],5),d["trailing_missing_frames"]) for s,d in items.items()])


if __name__ == "__main__":
    analyze(*record())
