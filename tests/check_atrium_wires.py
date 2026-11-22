"""比较 Sponza 中庭两排吊绳的 RenderDoc 光照图。各帧导出方法见 output/validation/atrium-aa/README.md。"""
import argparse
import hashlib
import json
import os
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont
import yaml
from project_atrium_wires import covered_pixels, project, strands


ROOT = Path(__file__).resolve().parents[1]
LUMA = np.array([.2126, .7152, .0722])


def load(work):
    report = json.loads((work / "replay.json").read_text())
    assert report["pack_event"] and not report["messages"], report
    assert report["tone_mapping"] == "ACES" and report["exposure_multiplier"] == 1
    width, height = report["size"]
    scene = work / "assets/scenes/Sponza-Atrium.redflag"
    settings = yaml.safe_load(Path(str(scene) + ".render.yaml").read_text(encoding="utf8"))
    log = (work / "editor.log").read_text(encoding="utf8", errors="replace")
    capture = Path((work / "frame-path.txt").read_text(encoding="utf8")).resolve(strict=True)
    receipt = {key: report[key] for key in ("frame", "size", "shading_size", "samples_per_pixel", "display_event",
                                             "pack_event", "messages", "tool")}
    receipt.update(scene_sha256=hashlib.sha256(scene.read_bytes()).hexdigest(), settings=settings,
                   capture=capture.relative_to(ROOT).as_posix(), work=work.resolve().relative_to(ROOT).as_posix(),
                   gpu=next(line.split("Vulkan device: ")[1] for line in log.splitlines() if "Vulkan device:" in line))
    image = np.fromfile(work / "display.f32", "<f4").reshape(height, width, 4)[:, :, :3]
    assert np.isfinite(image).all() and image.min() >= 0 and image.max() <= 1.00001
    return image, receipt


def reduce_reference(work, shape, receipt):
    width, height = receipt["shading_size"]
    h, w = shape[:2]
    assert (width, height) == (4*w, 4*h)
    # 四行四列属于同一个面板像素。和正式显示着色器一样，在线性光照上取平均后，再转为显示颜色。
    light = np.fromfile(work / "light.f32", "<f4").reshape(h, 4, w, 4, 4)[..., :3].mean((1, 3))
    return display_color(light)


def display_color(light):
    x = np.maximum(light, 0)
    mapped = np.clip(x * (2.51*x + .03) / (x * (2.43*x + .59) + .14), 0, 1)
    return np.where(mapped <= .0031308, mapped*12.92, 1.055*mapped**(1/2.4) - .055)


def as_image(image):
    return Image.fromarray(np.uint8(np.clip(image, 0, 1)*255 + .5))


def scores(image, reference, rows):
    y = np.arange(image.shape[0]) + .5
    band = np.zeros(image.shape, bool)
    contrast = []
    for _, x, center_y, width in rows:
        if width == 0:
            continue
        distance = abs(y - center_y)
        band[:, x] = distance <= 2
        # 以绳上下的背景作对照。原模型本来没有绳的列不参加断线统计。
        flank = (distance >= 2.5) & (distance <= 4.5)
        core = distance <= 1.25
        contrast.append([np.median(values[flank, x]) - values[core, x].min()
                         for values in (image, reference)])
    actual, expected = np.array(contrast).T
    distinguishable = expected >= .08
    error = (image-reference)[band]
    return dict(columns=len(contrast), band_pixels=int(band.sum()), mean_abs_luma=float(np.abs(error).mean()),
                mean_signed_luma=float(error.mean()),
                mean_abs_contrast=float(np.abs(actual-expected).mean()), high_contrast_columns=int(distinguishable.sum()),
                weak_columns=int(((actual < expected/3) & distinguishable).sum()))


def comparison(images, out, heading="Sponza 中庭的两排吊绳",
               caption="同一机位。每个场景像素放大 5 倍，下方绳的中央空隙也存在于原模型。"):
    font = Path(os.environ["WINDIR"]) / "Fonts/msyh.ttc"
    title, small = (ImageFont.truetype(str(font), size) for size in (22, 17))
    frame = Image.new("RGB", (964, 96 + len(images)*161), "#f6f5f1")
    draw = ImageDraw.Draw(frame)
    draw.text((22, 12), heading, fill="#27292c", font=title)
    draw.text((22, 49), caption, fill="#44474b", font=small)
    for i, (name, image) in enumerate(images):
        y = 94 + i * 161
        draw.text((22, y+47), name, font=small, fill="#33363a")
        # 两条绳分开裁，保留两侧建筑作为位置参照。沿像素边缘放大，避免图片软件再平滑。
        for x, crop in ((193, (257, 37, 331, 60)), (579, (257, 73, 331, 96))):
            frame.paste(as_image(image).crop(crop).resize((370, 115), Image.Resampling.NEAREST), (x,y))
        draw.text((193, y+118), "上方吊绳", font=small, fill="#55595c")
        draw.text((579, y+118), "下方吊绳", font=small, fill="#55595c")
    frame.save(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("baseline", "spatial", "reference"):
        parser.add_argument("--" + key, type=Path, required=True)
    parser.add_argument("--candidate", type=Path)
    parser.add_argument("--native", type=Path, nargs=2, metavar=("BASELINE", "SPATIAL"))
    parser.add_argument("--native-candidate", type=Path)
    parser.add_argument("--out", type=Path, default=ROOT / "output/validation/atrium-aa")
    args = parser.parse_args()
    args.out.mkdir(exist_ok=True, parents=True)
    base, before = load(args.baseline)
    spatial, after = load(args.spatial)
    _, high = load(args.reference)
    assert base.shape == spatial.shape == (385,600,3)
    assert before["samples_per_pixel"] == 1 and after["samples_per_pixel"] == high["samples_per_pixel"] == 4
    reference = reduce_reference(args.reference, base.shape, high)
    images = {"baseline":base, "spatial":spatial}
    receipts = {"baseline":before, "spatial":after, "reference":high}
    if args.candidate:
        images["candidate"], receipts["candidate"] = load(args.candidate)
        assert images["candidate"].shape == base.shape and receipts["candidate"]["samples_per_pixel"] == 1
    scene = args.baseline / "assets/scenes/Sponza-Atrium.redflag"
    rows = list(strands(*covered_pixels(project(600, 385, scene, None), 600, 385, 32)))
    result = {}
    target = reference @ LUMA
    for name, image in images.items():
        source = image @ LUMA
        result[name] = {rope:scores(source, target, [row for row in rows if row[0] == rope])
                        for rope in ("near", "far")}
        # 画面左侧近处红布，有刺绣和褶皱；同时检查全图与这里能否保住细节。
        delta = np.abs(source - target)
        result[name].update(whole_frame_mae=float(delta.mean()), near_red_curtain_mae=float(delta[202:290,30:103].mean()))
        as_image(image).save(args.out / (name+".png"))
    as_image(reference).save(args.out / "reference.png")
    labels = {"baseline":"原有 TAA", "spatial":"原有 TAA + 2×2", "candidate":"修改后的 TAA"}
    comparison([(labels[name], image) for name,image in images.items()] + [("4×4 空间参考",reference)],
               args.out / "wires.png")
    native = {}
    if args.native:
        for name, work in zip(("native_baseline", "native_spatial"), args.native):
            image, receipts[name] = load(work)
            native[name] = image @ LUMA
            as_image(image).save(args.out / (name+".png"))
        assert receipts["native_baseline"]["size"] == receipts["native_spatial"]["size"]
        assert receipts["native_spatial"]["samples_per_pixel"] == 4
    if args.native_candidate:
        image, receipts["native_candidate"] = load(args.native_candidate)
        assert receipts["native_candidate"]["samples_per_pixel"] == 1
        assert receipts["native_candidate"]["size"] == receipts["native_baseline"]["size"]
        as_image(image).save(args.out / "native_candidate.png")
        w, h = receipts["native_candidate"]["size"]
        rows = list(strands(*covered_pixels(project(w, h, scene, None), w, h, 32)))
        native["native_candidate"] = image @ LUMA
        for name in ("native_baseline", "native_candidate"):
            result[name+"_vs_native_spatial"] = {
                rope:scores(native[name], native["native_spatial"], [r for r in rows if r[0] == rope])
                for rope in ("near", "far")}
    for receipt in receipts.values():
        assert receipt["scene_sha256"] == before["scene_sha256"]
        assert receipt["frame"] == before["frame"]
        # 除画幅和空间采样外，相机、显示、光照和时间采样设置必须匹配。
        key = lambda settings: {k:v for k,v in settings.items() if k not in ("PreviewWidth", "Supersampling")}
        assert key(receipt["settings"]) == key(before["settings"])
    report = dict(frames=receipts, scores=result, curtain_xyxy=[30,202,103,290],
                  reference_samples_per_output_pixel=16, reference_uses_taa=True)
    (args.out / "comparison.json").write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf8")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
