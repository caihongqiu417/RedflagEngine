"""在仓库根目录运行 tests/run_mask_camera_motion.cmd，对照 Vulkan 铁丝网轮廓与像素面积积分。"""
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
OUT = ROOT / "output/validation/fine-mask-motion"
SIZE = 64


def hash_file(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def coverage_reference(rows):
    # 每四列中 [0,1) 是实心。下面求从纹理左边到 t 的实心宽度，两端相减即是一个像素里实际盖住的部分。
    def solid_width(t):
        return np.floor(t / 4) + np.minimum(t % 4, 1)

    result = []
    sample_widths = []
    max_error = 0.
    for row in rows:
        eye_x, eye_z = float(row["eye_x"]), float(row["eye_z"])
        p00 = float(row["projection_x"])
        width = 1024 * eye_z / (SIZE * p00)
        edges = (eye_x + 1) * 512 + (np.arange(SIZE + 1) - SIZE / 2) * width
        coverage = np.diff(solid_width(edges)) / width
        # 各像素再作 4096 次等距取样，独立核对积分、纹素中心和相机投影的换算。
        t = edges[:-1, None] + (np.arange(4096) + .5) * width / 4096
        samples = np.mean(t % 4 < 1, axis=1)
        max_error = max(max_error, np.max(np.abs(samples - coverage)))
        result.append(coverage)
        sample_widths.append(width)
    assert max_error < .0005, max_error
    return np.array(result), np.array(sample_widths), float(max_error)


def contact(path, tracks):
    font_path = Path(os.environ.get("WINDIR", "C:/Windows")) / "Fonts/msyh.ttc"
    title = ImageFont.truetype(str(font_path), 23)
    text = ImageFont.truetype(str(font_path), 17)
    small = ImageFont.truetype(str(font_path), 14)
    canvas = Image.new("RGB", (930, 590), "#f8f8f5")
    pen = ImageDraw.Draw(canvas)
    pen.text((24,14), "移动相机时的铁丝覆盖率", fill="#24282a", font=title)
    pen.text((24,50), "每行取同一帧的中央 64 个像素。向下是下一帧，深色表示铁丝所占面积较大。", fill="#40464a", font=text)

    def strip(image):
        # 取样以原始大小进行，展示才放大。白色代表空隙，接近黑色的像素代表全部被铁丝遮住。
        return Image.fromarray(np.uint8(244 - np.clip(image, 0, 1) * 209)).convert("RGB")

    for index, (label, rendered, reference) in enumerate(tracks):
        top = 96 + index * 240
        pen.text((24,top), label, fill="#30383b", font=text)
        height = 155
        for x, image, name in ((24,rendered,"Vulkan 实际绘制"),(484,reference,"按像素面积计算")):
            block = strip(image).resize((416, height), Image.Resampling.NEAREST)
            canvas.paste(block, (x,top+36))
            pen.rectangle((x,top+36,x+415,top+35+height), outline="#babfbd")
            pen.text((x,top+height+47), name, fill="#40464a", font=small)
    canvas.save(path)


def analyze(exe, folder):
    rows = list(csv.DictReader((folder / "frames.tsv").open(), delimiter="\t"))
    raster = np.fromfile(folder / "raster.u8", "u1").reshape(len(rows),SIZE,SIZE)
    picks = np.fromfile(folder / "center-picks.i32", "<i4").reshape(len(rows),SIZE)
    actual = raster[:,SIZE//2,:].astype(float)
    # 铁丝是竖直线，整图各行应一致。拾取来自另一遍正式的实体编号绘制，按整个中间行核对。
    assert np.array_equal(raster, np.broadcast_to(actual[:,None,:],raster.shape))
    ids = np.array([int(r["fence_id"]) for r in rows])[:,None]
    assert np.array_equal(picks, np.where(actual == 1,ids,-1))
    reference, widths, ref_error = coverage_reference(rows)
    assert np.array_equal(actual[0], (reference[0] >= .5).astype(float))  # 放大时逐列核对实际投影位置。

    OUT.mkdir(exist_ok=True, parents=True)
    summary = {}
    contact_tracks = []
    records = []
    for name in ("magnified","fixed","zoom","pan"):
        positions = np.array([i for i,r in enumerate(rows) if r["track"] == name])
        a, b = actual[positions],reference[positions]
        avg, ideal = a.mean(axis=1),b.mean(axis=1)
        info = dict(frames=len(positions), actual_min=float(avg.min()), actual_max=float(avg.max()),
                    reference_min=float(ideal.min()), reference_max=float(ideal.max()),
                    mean_absolute_coverage_error=float(np.abs(avg - ideal).mean()))
        if len(positions) > 1:
            # 先算正确投影产生的逐像素变化，再从测得的变化中减去它。这会保留轮廓方向和消失/出现的符号。
            da, db = np.diff(a,axis=0),np.diff(b,axis=0)
            areas = np.abs(np.diff(avg))
            pair = int(np.argmax(areas))
            info.update(peak_area_change=float(areas[pair]), reference_area_change_same_pair=float(abs(ideal[pair+1]-ideal[pair])),
                        peak_from_frame=pair, peak_to_frame=pair+1,
                        max_mean_pixel_change=float(np.max(np.abs(da).mean(axis=1))),
                        max_reference_pixel_change_same_pair=float(np.max(np.abs(db[pair]))),
                        actual_mean_pixel_change_same_pair=float(np.abs(da[pair]).mean()),
                        reference_mean_pixel_change_same_pair=float(np.abs(db[pair]).mean()),
                        mean_absolute_temporal_error=float(np.abs(da-db).mean()),
                        frames_with_area_change=int(np.count_nonzero(areas)))
        if name in ("zoom","pan"):
            label = "后退  ·  从 1 到 4 纹素 / 像素" if name == "zoom" else "平移  ·  每帧移动 1/16 个纹素，距离固定"
            contact_tracks.append((label,a,b))
        if name == "zoom":
            visible = np.flatnonzero(avg > 0)
            first = int(visible[-1] + 1) if visible.size else 0
            info["trailing_invisible_frames"] = len(positions) - first
            if first < len(positions):
                info["first_persistently_invisible_frame"] = first
                info["texels_per_pixel_at_disappearance"] = float(widths[positions[first]])
        for frame, i in enumerate(positions):
            change = np.abs(a[frame] - a[frame-1]).mean() if frame else 0
            change_ref = np.abs(b[frame] - b[frame-1]).mean() if frame else 0
            records.append(dict(track=name,frame=frame,eye_x=rows[i]["eye_x"],eye_z=rows[i]["eye_z"],
                                texels_per_pixel=widths[i],fence_pixels=int(raster[i].sum()),
                                pick_hits_on_center_row=int((picks[i] != -1).sum()), coverage=avg[frame],
                                reference=ideal[frame],mean_pixel_change=change,reference_pixel_change=change_ref))
        summary[name] = info

    assert summary["fixed"]["peak_area_change"] == 0 and summary["fixed"]["max_mean_pixel_change"] == 0
    # 水平视野恰好容纳 24 个完整周期。相机向旁边移动后，理论面积不应改变。
    assert np.allclose([summary["pan"]["reference_min"],summary["pan"]["reference_max"]], .25, atol=1e-6)
    with (OUT / "frames.csv").open("w",newline="",encoding="utf8") as csv_file:
        writer = csv.DictWriter(csv_file,fieldnames=list(records[0]))
        writer.writeheader()
        writer.writerows(records)
    contact(OUT / "motion.png",contact_tracks)
    # 保存原始覆盖和拾取，以及对应帧的面积参照，便于独立检查 CSV 之外的任意列。
    np.savez_compressed(OUT / "pixels.npz", coverage=raster, picks=picks, reference=reference)
    gpu = (folder / "gpu.txt").read_text().splitlines()
    result = dict(device=gpu[0], raw_driver=int(gpu[1]), build="Vulkan Debug x64", viewport=[SIZE,SIZE],
                  exe_sha256=hash_file(exe), library_sha256=hash_file(ROOT/"bin/Debug-windows-x86_64/Redflag/Redflag.lib"),
                  input_sha256=hash_file(folder/"mask.png"), cutoff=.5, fov_degrees=45,
                  total_frames=len(rows), all_decoded_pixels=int(raster.size), center_row_pick_reads=int(picks.size),
                  mismatched_picks=0, exact_reference_max_error_against_4096_subsamples=ref_error,
                  reference_unit="fraction of one pixel covered by opaque wires", tracks=summary)
    (OUT / "report.json").write_text(json.dumps(result,indent=2)+"\n",encoding="utf8")
    print(json.dumps(result,ensure_ascii=False))


if __name__ == "__main__":
    exe = ROOT / "bin-int/Tests/MaskCameraMotion.exe"
    work = ROOT / "tmp/mask-camera-motion"
    folder = prepare(work,False,True)
    env = dict(os.environ, RF_VULKAN_VALIDATION="1")
    env["PATH"] = str(ROOT / "bin/Debug-windows-x86_64/RedflagForge") + os.pathsep + env["PATH"]
    with (work / "native.log").open("wb") as log:
        subprocess.run([str(exe),str(folder)],cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
    analyze(exe,folder)
