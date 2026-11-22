"""用实时探针、固定的烘焙探针和关闭间接光的 Sponza 帧，分别检查吊绳的颜色。"""
import argparse
import hashlib
import json
import os
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

from check_atrium_wires import ROOT, LUMA, as_image, comparison, display_color, load, reduce_reference, scores
from project_atrium_wires import project, covered_pixels, strands


def camera(work):
    values = np.fromfile(work / "taa-camera.bin", "<f4")
    assert len(values) == 100
    projection, view, _, _, stable = values[:80].reshape(5, 4, 4).transpose(0, 2, 1)
    width, height, frame, _ = map(int, values[84:88].view("<u4"))
    # stable 是未抖动的投影乘以观察矩阵。减去它就能核对当前帧的采样偏移，屏幕 y 坐标向下。
    p = stable @ np.linalg.inv(view)
    shift = (projection[:2, 2] - p[:2, 2]) / projection[3, 2] * [width/2, -height/2]
    return dict(shading_size=[width, height], render_frame_index=frame, pixel_shift=shift.tolist(),
                stable_matrix=stable.tolist(), eye=values[80:83].tolist(), blend=float(values[92]),
                edge_blend=float(values[99]))


def rope_scores(pixels, reference, geometry):
    return {name:scores(pixels, reference, [r for r in geometry if r[0] == name]) for name in ("near","far")}


def compare_phase(folder16, folder32, rows):
    first, last = camera(folder16), camera(folder32)
    assert first["render_frame_index"] + 16 == last["render_frame_index"]
    assert np.array_equal(first["pixel_shift"], last["pixel_shift"])
    assert np.array_equal(first["stable_matrix"], last["stable_matrix"])
    surface = lambda p: np.fromfile(p / "surface.f32", "<f4")
    positions = lambda p: np.fromfile(p / "taa-position.f32", "<f4").reshape(385, 600, 4)[:, :, :3]
    assert np.array_equal(surface(folder16), surface(folder32))
    assert np.array_equal(positions(folder16), positions(folder32))
    raw = lambda p: np.fromfile(p / "taa-input.f32", "<f4").reshape(385, 600, 4)[:, :, :3] @ LUMA
    a, b = raw(folder16), raw(folder32)
    report = dict(frames=[16, 32], same_materials_positions_and_sample=True, frame16=first, frame32=last)
    for strand in ("near", "far"):
        measured = scores(b, a, [r for r in rows if r[0] == strand])
        report[strand] = dict(mean_abs_linear_luma_change=measured["mean_abs_luma"],
                              mean_signed_linear_luma_change=measured["mean_signed_luma"])
    return report


def final_figure(before, after, reference, out):
    page = Image.new("RGB", (1184, 532), "#f6f5f1")
    pen = ImageDraw.Draw(page)
    font = Path(os.environ["WINDIR"]) / "Fonts/msyh.ttc"
    title, text, small = (ImageFont.truetype(str(font), size) for size in (26, 19, 16))
    pen.text((22, 12), "Sponza 中庭上方的吊绳", font=title, fill="#26282a")
    pen.text((22, 56), "同一份烘焙光照，同一机位。左图圈出两排绳的位置，右侧各取一段放大三倍。", font=text, fill="#414446")
    panel = as_image(after).resize((540,347), Image.Resampling.LANCZOS)
    page.paste(panel,(22,112))
    for a, b in ((34,62), (71,99)):
        rect = (22+round(251*.9),112+round(a*347/385),22+round(338*.9),112+round(b*347/385))
        pen.rectangle(rect, outline="#98fbff", width=2)
    pen.text((22, 468), "Vulkan 场景面板 · 600 × 385", font=small, fill="#494d50")
    pen.text((614, 92), "上方吊绳", font=small, fill="#484b4e")
    pen.text((914, 92), "下方吊绳", font=small, fill="#484b4e")
    for i, (label, image) in enumerate((("原有 TAA", before), ("修改后", after), ("高采样参考", reference))):
        top = 119 + 130*i
        pen.text((614,top), label, font=small, fill="#343739")
        for x, region in ((614,(251,34,338,62)),(914,(251,71,338,99))):
            patch = as_image(image).crop(region).resize((261,84), Image.Resampling.NEAREST)
            page.paste(patch,(x, top+26))
    page.save(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--old", required=True, help="初版可执行文件 SHA256 的前 12 位")
    parser.add_argument("--compare", nargs="+", required=True, help="参与比较的各版 SHA 前 12 位，可写成 SHA=图中名称")
    parser.add_argument("--out", type=Path, default=ROOT / "output/validation/atrium-aa/lighting.json")
    parser.add_argument("--final", help="在固定探针的全景图中比较该版与原版")
    args = parser.parse_args()
    directory = ROOT / "tmp/material-reference"
    reports, input_checks = {}, {}
    labels = {args.old:"原有 TAA"}
    phase_images = []
    for tag in args.compare:
        key, _, label = tag.partition("=")
        labels[key] = label or key
    args.out.parent.mkdir(parents=True, exist_ok=True)
    cases = (("live", "Wire-Baseline", "Wire-Uniform-Reference"), ("baked", "FixedGI-Sponza", "FixedGI-Reference"),
             ("off", "NoGI-Sponza", "NoGI-Reference"))
    for name, stem, high in cases:
        old = directory / f"{stem}-{args.old}-600-f32"
        if not (old / "replay.json").is_file():
            continue
        scene = old / "assets/scenes/Sponza-Atrium.redflag"
        geometry = list(strands(*covered_pixels(project(600, 385, scene, None), 600, 385, 32)))
        earlier = directory / f"{stem}-{args.old}-600-f16"
        if (earlier / "taa-camera.bin").is_file():
            input_checks[name] = compare_phase(earlier, old, geometry)
            if name != "off":
                title = "实时探针" if name == "live" else "固定探针"
                for number, folder in ((16, earlier), (32, old)):
                    raw = np.fromfile(folder / "taa-input.f32", "<f4").reshape(385, 600, 4)[:, :, :3]
                    phase_images.append((f"{title} · 第 {number} 帧", display_color(raw)))
        reference_dir = directory / f"{high}-{args.old}-1200-f32"
        if not (reference_dir / "replay.json").is_file():
            continue
        _, receipt = load(reference_dir)
        uniforms = camera(reference_dir)
        assert receipt["settings"]["HistoryWeight"] == 1 and uniforms["blend"] == 31/32
        ref_rgb = reduce_reference(reference_dir, (385,600,3), receipt)
        ref = ref_rgb @ LUMA
        crops = []
        group = dict(reference=receipt, camera_reference=uniforms, versions={})
        recent = None
        if name == "live":
            folder = directory / f"Wire-Spatial-{args.old}-1200-f32"
            _, details = load(folder)
            recent = reduce_reference(folder, (385,600,3), details) @ LUMA
            group["recent_reference"] = dict(receipt=details, metrics={})
        if name == "baked":
            probe_file = scene.with_suffix(scene.suffix + ".rfprobes")
            group["probe_sha256"] = hashlib.sha256(probe_file.read_bytes()).hexdigest()
        for version, label in labels.items():
            folder = directory / f"{stem}-{version}-600-f32"
            if not (folder / "replay.json").is_file():
                continue
            image, run = load(folder)
            settings = camera(folder)
            assert np.allclose(settings["stable_matrix"], uniforms["stable_matrix"], atol=1.e-6)
            assert settings["render_frame_index"] == uniforms["render_frame_index"] == 31
            luminance = image @ LUMA
            measured = rope_scores(luminance, ref, geometry)
            group["versions"][version] = dict(label=label, receipt=run, camera=settings, metrics=measured)
            if recent is not None:
                group["recent_reference"]["metrics"][version] = rope_scores(luminance, recent, geometry)
            crops.append((label,image))
        if name != "off":
            title = "实时探针" if name == "live" else "固定探针"
            comparison(crops + [("高采样参考",ref_rgb)], args.out.parent / (name + "-wires.png"),
                       f"{title}下的两排吊绳 · 第 32 帧",
                       "同一视角，放大 5 倍。参考图每个输出像素在 16 个空间位置上分别平均 32 次着色。")
        if name == "baked" and args.final:
            before, _ = load(old)
            after, _ = load(directory / f"{stem}-{args.final}-600-f32")
            final_figure(before, after, ref_rgb, args.out.parent / "final-wires.png")
        if name == "live" and args.final:
            after, _ = load(directory / f"{stem}-{args.final}-600-f32")
            as_image(after).save(args.out.parent / "final-scene.png")
            native = directory / f"{stem}-{args.final}-0-f32"
            if (native / "replay.json").is_file():
                image, details = load(native)
                assert details["size"] == details["shading_size"]
                group["native_final"] = dict(receipt=details, camera=camera(native))
                as_image(image).save(args.out.parent / "native-final.png")
                w,h = details["size"]
                native_geometry = list(strands(*covered_pixels(project(w,h,scene,None), w, h, 32)))
                old_image, _ = load(directory / f"{stem}-{args.old}-0-f32")
                reference_image, receipt = load(directory / f"Wire-Spatial-{args.old}-0-f32")
                group["native_reference"] = dict(receipt=receipt, metrics={
                    version:rope_scores(pixels @ LUMA, reference_image @ LUMA, native_geometry)
                    for version,pixels in ((args.old,old_image), (args.final,image))})
        reports[name] = group
    if phase_images:
        comparison(phase_images, args.out.parent / "lighting-change.png", "同一个采样位置的光照变化",
                   "绘制第 16 和 32 帧时，相机、网格和采样位置完全相同。图中显示的是进入 TAA 之前的颜色。")
    args.out.write_text(json.dumps(dict(cases=reports, same_phase=input_checks), indent=2) + "\n", encoding="utf8")
    for name, group in reports.items():
        for version, data in group["versions"].items():
            print(name, version, [round(data["metrics"][rope]["mean_abs_luma"],5) for rope in ("near", "far")])
    for name, check in input_checks.items():
        print(name, "linear change 16 to 32", [check[rope]["mean_abs_linear_luma_change"] for rope in ("near", "far")])


if __name__ == "__main__":
    main()
