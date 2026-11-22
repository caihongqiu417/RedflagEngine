"""比较 RenderDoc 导出的场景颜色，生成火盆局部图，并核验无发光材质的摄影棚。"""
import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
from PIL import Image


def pixels(work, name):
    width, height = json.loads((work / "replay.json").read_bytes())["size"]
    return np.fromfile(work / (name + ".f32"), "<f4").reshape(height, width, 4)


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    args = argparse.ArgumentParser(description=__doc__)
    for case in ("temple_before", "temple_after", "camera_before", "camera_after"):
        args.add_argument(case, type=Path)
    args = args.parse_args()
    root = Path(__file__).resolve().parents[1]
    out = root / "output/profiling/emissive-material"
    before, after = args.temple_before, args.temple_after
    for field in ("light", "display"):
        assert np.array_equal(pixels(before, field), pixels(after, field))
    e = pixels(after, "emission")[:, :, :3]
    assert np.array_equal(pixels(before, "emission")[:, :, :3], e)
    d = pixels(after, "display")[:, :, :3]
    Image.fromarray(np.rint(255 * np.clip(d, 0, 1)).astype("uint8")).save(out / "temple.png")
    # 用回放读出的非零发光像素标出火炭，灰色保留同一张面板图上的火盆轮廓。
    rect = np.s_[234:350, 345:479]
    mask = np.any(e > 0, axis=2)
    gray = np.rint(255 * (.15 + .48 * (d[rect] @ [.2126, .7152, .0722])))
    crop = np.where(mask[rect][:, :, None], [255, 143, 23], gray[:, :, None])
    Image.fromarray(crop.astype("uint8")).save(out / "temple-emission.png")
    report = dict(temple=dict(lit_pixels=int(mask.sum()), matched_rgba_pixels=d.shape[0]*d.shape[1],
                              crop_xywh=[345,234,134,116]), camera={})
    a, b = args.camera_before, args.camera_after
    for label, work in (("before", a), ("after", b)):
        row = json.loads((work / "replay.json").read_bytes())
        path = Path((work / "frame-path.txt").read_text(encoding="utf8"))
        report["camera"][label] = dict(capture=path.relative_to(root).as_posix(), sha256=digest(path),
            frame=row["frame"], size=row["size"], draws=len(row["draws"]), dispatches=row["dispatches"],
            display_sha256=digest(work / "display.f32"), light_sha256=digest(work / "light.f32"))
    for field in ("surface", "light", "display"):
        assert np.array_equal(pixels(a, field), pixels(b, field))
    (out / "pixels.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf8")


if __name__ == "__main__":
    main()
