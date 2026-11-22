"""只用 CPU 比较四个子像素如何估算覆盖面积。输入为同一轨迹的实际 GPU 读回和解析面积。"""
import json
from pathlib import Path
import numpy as np

from check_mask_supersampling import ARCHIVE, SIZE, cameras, wire_area


def texture(x, width):
    # 夹具每四列有一列白。第一级缩图的两个值为 128/255 和 0，之后每个像素均为 64/255。
    lod = np.clip(np.log2(width), 0, 2)
    values = []
    for i, colors in enumerate(([1,0,0,0], [128/255,0], [64/255])):
        texel = x / 2**i - .5
        left = np.floor(texel).astype(int)
        f = texel - left
        c = np.asarray(colors)
        values.append((1-f)*c[left % len(c)] + f*c[(left+1) % len(c)])
    return ((1-lod.clip(0,1))*values[0] + (1-np.abs(lod-1))*values[1] + (lod-1).clip(0,1)*values[2])


def predictions(rows):
    w = np.array([float(r["texels_per_pixel"]) for r in rows])[:,None]
    x = 512 + 512*np.array([float(r["eye_x"]) for r in rows])[:,None] + (np.arange(SIZE)-31.5)*w
    # 比较中央一行。所有相机轨迹在这一行的纵向都完全落在面板内，26.5 纹素/像素时保留左右外沿。
    full_alpha = texture(x, w)
    sub_mix = np.log2(w/2).clip(0,1)
    pixel_mix = np.log2(w).clip(0,1)
    sub = [texture(x + offset*w, w/2) for offset in (-.25,.25)]
    avg = .5 * (sub[0] + sub[1])
    narrow = .25 * sum(texture(x + off*w, w/4) for off in (-.375,-.125,.125,.375))
    predictions = {}
    for name, center, blend in (("gpu_model", None, sub_mix), ("common_mip", full_alpha, pixel_mix),
                                ("two_lookups", avg, pixel_mix), ("four_lookups", narrow, pixel_mix)):
        counts = np.zeros_like(x)
        for i,t in enumerate((.125,.625,.875,.375)):
            k = i%2
            sample_x = x + (-.25 if k == 0 else .25)*w
            alpha = sub[k] if center is None else sub[k] + blend * (center-sub[k])
            counts += ((alpha >= .5 + (t-.5)*blend) & (sample_x >= 0) & (sample_x < 1024))
        predictions[name] = counts/4
    return predictions


def evaluate(image, reference):
    error = image-reference
    return {"spatial_mae":float(np.abs(error).mean()), "temporal_mae":float(np.abs(np.diff(error,axis=0)).mean()),
            "peak_area_error":float(np.abs(error.mean(axis=1)).max()), "area_mean":float(image.mean())}


def main():
    rows = cameras()
    tracks = np.array([r["track"] for r in rows])
    ref = wire_area(rows)[:,32]
    saved = np.load(ARCHIVE / "linear-center-rows.npz")
    assert np.max(np.abs(saved["reference"]-ref)) < 1.e-7
    images = predictions(rows)
    for version in ("1","2","production-2"):
        images["gpu_"+version] = (saved[version+"_palette0"][:,:,0]-.125)/(4-.125)
    # 如果事先知道正确面积，四个二值样本只能给出最接近的 0、1/4、1/2、3/4 或 1。
    images["nearest_quarter_oracle"] = np.floor(4*ref+.5)/4
    results = {name:{key:evaluate(img[tracks==name],ref[tracks==name]) for key,img in images.items()}
               for name in ("zoom","pan","far8","far26.5")}
    modeled = images["gpu_model"]
    actual = images["gpu_production-2"]
    calibration = {name:float(np.mean(modeled[tracks==name] != actual[tracks==name])) for name in results}
    data = dict(scope="CPU diagnostics at the 64 central-row pixels only. GPU columns read the archived native results. "
                    "CPU uses ideal trilinear mip filtering of 8-bit alpha. Razor-thin vertical binary stripes, frontal camera. "
                    "No GPU times, production changes or assumptions about arbitrary textures.", calibration=calibration, results=results)
    print(json.dumps(data,indent=2))
    (ARCHIVE / "pixel-footprint-cpu.json").write_text(json.dumps(data,indent=2)+"\n",encoding="utf8")


if __name__ == "__main__":
    main()
