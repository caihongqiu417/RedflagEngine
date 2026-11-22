"""Measure shimmer across frames and compare each frame with the mean of the full jitter pattern."""
from argparse import ArgumentParser
from hashlib import sha256
from pathlib import Path
import json
import numpy as np
from PIL import Image

root = Path(__file__).resolve().parents[1]
out = root / "output/validation/temporal"
out.mkdir(parents=True, exist_ok=True)
shape = (91, 161, 4)
luma = np.array([.2126, .7152, .0722], dtype=np.float32)


def gather(folder, stem, frames):
    return np.stack([np.fromfile(folder / f"{stem}-{i}.f32", "<f4").reshape(shape)[:, :, :3] for i in frames])


def display(a):
    x = np.maximum(a * 4, 0)
    x /= 1 + x
    return np.where(x <= .0031308, x * 12.92, 1.055 * x ** (1 / 2.4) - .055)


def measure(movie, reference):
    def rms(values):
        return float(np.sqrt(np.mean(np.square(values, dtype=np.float64))))

    # 屏幕上看到的明暗变化，用固定曝光后的亮度衡量。纹理清晰度另与同一镜头的多采样参考比较。
    ref = display(reference) @ luma
    lum = display(movie) @ luma
    change = lum[1:] - lum[:-1]
    radiance_error = movie - reference
    gx = np.diff(lum.mean(axis=0) - ref, axis=1).ravel()
    gy = np.diff(lum.mean(axis=0) - ref, axis=0).ravel()
    return {
        "adjacent_frame_luma_rms": rms(change),
        "adjacent_frame_p95_absolute_luma": float(np.percentile(np.abs(change), 95)),
        "gradient_error_rms": rms(np.concatenate([gx, gy])),
        "linear_rgb_mse": rms(radiance_error) ** 2,
    }


if __name__ == "__main__":
    parser = ArgumentParser()
    parser.add_argument("--record-before", type=Path, help="Archive readbacks from the pre-fix build")
    args = parser.parse_args()
    baseline = out / "before.npz"
    if args.record_before:
        old = gather(args.record_before, "static-taa", range(16, 32))
        period = 32 if (args.record_before / "static-input-31.f32").exists() else 16
        inputs = gather(args.record_before, "static-input", range(period))
        np.savez_compressed(baseline, movie=old, input_hash=sha256(inputs.tobytes()).hexdigest())
        print("Recorded", baseline)
    else:
        folder = root / "tmp/screen-effects"
        actual = gather(folder, "static-taa", range(16, 32))
        inputs = gather(folder, "static-input", range(32))
        reference = inputs.mean(axis=0)
        with np.load(baseline) as stored:
            before = stored["movie"]
            same_input = str(stored["input_hash"]) == sha256(inputs.tobytes()).hexdigest()
        old_stats, new_stats = measure(before, reference), measure(actual, reference)
        if not same_input:
            # 旧帧的拍摄位置不同。相邻帧之间的亮度变化仍可比较，空间误差只能与各自的参考图比较。
            old_stats = {key: value for key, value in old_stats.items() if key.startswith("adjacent_frame_")}
        def example(movie):
            pair = display(movie[:2, 61, 98, :]) @ luma
            return {"frame17": float(pair[0]), "frame18": float(pair[1]), "difference": float(pair[1] - pair[0])}
        report = {"scene": "Khronos Sponza", "render_size": [161, 91], "samples_per_period": len(inputs),
                  "compared_frames_zero_based": [16, 31], "input_matches_original_device": same_input,
                  "display_exposure": 4, "pixel_differences": (len(actual)-1)*shape[0]*shape[1],
                  "example_pixel": {"x": 98, "y": 61, "before": example(before), "after": example(actual)},
                  "before": old_stats, "after": new_stats}
        print(json.dumps(report, indent=2))
        # 固定输入时和原问题逐项对比。跨驱动输入有差异时另用当前参考检查，不把不可比的旧数据混进结论。
        if same_input:
            for metric in ("adjacent_frame_luma_rms", "gradient_error_rms", "linear_rgb_mse"):
                if not new_stats[metric] < old_stats[metric]:
                    raise ValueError(f"Temporal regression: {metric}")
            if new_stats["adjacent_frame_luma_rms"] > .01 or new_stats["gradient_error_rms"] > .005:
                raise ValueError("Static Sponza exceeds the measured shimmer or blur tolerance")
        else:
            if new_stats["linear_rgb_mse"] >= measure(inputs, reference)["linear_rgb_mse"]:
                raise ValueError("Temporal accumulation failed to reduce the single-frame reference error")
        (out / "metrics.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf8")
        for name, rgb in (("before", before[-1]), ("after", actual[-1]), ("reference", reference)):
            Image.fromarray(np.uint8(np.clip(display(rgb), 0, 1) * 255 + .5)).save(out / (name + ".png"))
        if same_input:
            for name, movie in (("before-motion", before), ("after-motion", actual)):
                # 同一倍率显示相邻帧差，越亮表示静止镜头下像素变化越大。
                delta = np.mean(np.abs(np.diff(display(movie) @ luma, axis=0)), axis=0)
                Image.fromarray(np.uint8(np.minimum(delta * 8, 1) * 255 + .5)).save(out / (name + ".png"))
