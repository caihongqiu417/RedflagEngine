"""比较同一份线性光照在两条显示曲线下的颜色，输入为 RenderDoc 实际导出的帧。"""
import argparse
import json
from pathlib import Path

import numpy as np

from check_atrium_sky import read_frame


def saturation(rgb):
    largest = rgb.max(axis=-1)
    return (largest - rgb.min(axis=-1)) / np.maximum(largest, 1e-6)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reinhard", type=Path, required=True)
    parser.add_argument("--aces", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    old, old_light, old_material, old_frame = read_frame(args.reinhard, args.out / "reinhard.png")
    new, new_light, new_material, new_frame = read_frame(args.aces, args.out / "after.png")
    assert old_frame["tone_mapping"] == "Reinhard" and new_frame["tone_mapping"] == "ACES"
    assert old_frame["exposure_multiplier"] == new_frame["exposure_multiplier"] == 1.0
    assert np.array_equal(old_light, new_light), "The two editor frames received different linear HDR inputs"
    assert np.array_equal(old_material, new_material), "Camera or material differs between the two frames"
    assert np.array_equal(np.fromfile(args.reinhard / "surface.f32", "<f4"),
                          np.fromfile(args.aces / "surface.f32", "<f4")), "The base colors differ"

    height, width = old_light.shape[:2]
    albedo = np.fromfile(args.aces / "surface.f32", "<f4").reshape(height, width, 4)[:, :, :3]
    # 按材质底色挑出红色或蓝色占优且较鲜明的样本，避开灰石墙。同一批位置用于比较两张图。
    vivid = (new_material != 0) & (saturation(albedo) > .65)
    dominant = albedo.argmax(axis=2)
    report = dict(frames={"Reinhard": old_frame, "ACES": new_frame}, equal_linear_hdr=True,
                  base_color_saturation_above=.65, colors={})
    for label, channel in (("red", 0), ("blue", 2)):
        selected = vivid & (dominant == channel)
        count = int(selected.sum())
        assert count, "Use a scene with both red and blue materials"
        stats = dict(pixels=count)
        for name, image in (("Reinhard", old), ("ACES", new)):
            samples = image[selected]
            stats[name] = dict(mean_display_saturation=float(saturation(samples.astype(float)).mean()),
                               median_rgb8=np.median(samples, axis=0).tolist())
        report["colors"][label] = stats
    report["pixel_count"] = width * height
    report["any_display_channel_255"] = {name: int(np.any(image == 255, axis=2).sum())
                                         for name, image in (("Reinhard", old), ("ACES", new))}
    (args.out / "curves.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf8")
    print(args.out)


if __name__ == "__main__":
    main()
