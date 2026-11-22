"""用相同的竖纹和斜纹相机轨迹，在 CPU 上比较四样点的阈值与排名。"""
import json
import numpy as np

from check_mask_supersampling import SIZE, cameras, frame_rgb
from compare_mask_footprint_gpu import WORK, OUT, diagonal_reference, wire_area
from compare_mask_pixel_footprint import texture


def run(geometry, rows, mip_width):
    u, v = (1., 0.) if geometry == "vertical" else (.8, .6)
    w = np.array([float(r["texels_per_pixel"]) for r in rows])[:,None,None]
    x = np.arange(SIZE)[None,None,:] - 31.5
    y = np.arange(SIZE)[None,:,None] - 31.5
    eye = 512 * np.array([float(r["eye_x"]) for r in rows])[:,None,None]
    tracks = np.array([r["track"] for r in rows])
    reference = wire_area(rows) if v == 0 else diagonal_reference(rows)[0]
    thresholds = np.array([.125, .625, .875, .375])
    samples, active = [], []
    for dy in (-.25,.25):
        for dx in (-.25,.25):
            sx, sy = eye + (x+dx)*w, (y+dy)*w
            samples.append(texture(512+u*sx+v*sy, w*mip_width/2))
            active.append((np.abs(sx)<512) & (np.abs(sy)<512))
    samples = np.stack(samples,-1)
    active = np.stack(active,-1)
    mean = samples.mean(axis=-1,keepdims=True)
    binary = (samples>=.5).mean(axis=-1,keepdims=True)
    ranks = np.zeros(samples.shape,dtype=np.int8)
    for i in range(4):
        # 同一个 alpha 值可能出现在多个位置。固定打破平局，每次才能恰好分到 0、1、2、3。
        earlier = thresholds < thresholds[i]
        value = samples[...,i,None]
        ranks[...,i] = ((samples>value) | ((samples==value) & earlier)).sum(axis=-1)
    assert np.all(np.sort(ranks,axis=-1)==[0,1,2,3])
    result = {}
    for offset in (0,.5,1):
        mix = np.clip(np.log2(w*mip_width/2) + offset,0,1)[...,None]
        alpha = samples + mix*(mean-samples)
        candidates = {
            "fixed_order": alpha >= .5 + mix*(thresholds-.5),
            "rank": alpha >= .5 + mix*((ranks+.5)/4-.5),
            "rank_count": ranks < np.floor(4*(binary + mix*(mean-binary))+.5),
        }
        for mode, hits in candidates.items():
            area = (hits & active).mean(axis=-1)
            error = area-reference
            metrics = {}
            for track in ("zoom","pan","far8","far26.5"):
                e = error[tracks==track]
                metrics[track] = [float(np.abs(e).mean()),float(np.abs(np.diff(e,axis=0)).mean())]
            result[f"{mode}_lod_plus_{offset}"] = metrics
    for name in ("first","candidate"):
        rgb = frame_rgb(WORK/geometry/name/"bin-int/Tests/gi-mask/production-2-0.f32",len(rows))
        error = (rgb[...,0]-.125)/3.875-reference
        result["measured_"+name] = {track:[float(np.abs(error[tracks==track]).mean()),
            float(np.abs(np.diff(error[tracks==track],axis=0)).mean())] for track in ("zoom","pan","far8","far26.5")}
    return result


if __name__ == "__main__":
    rows=cameras()
    report = dict(scope="CPU estimates. Two values per track: spatial and temporal mean absolute coverage error. "
                  "Mip widths use the larger axis of the UV derivative; a second diagonal estimate tests sensitivity "
                  "to the hardware mip approximation. The measured entries are the two actual GPU captures.")
    for geometry, width in (("vertical",1.),("diagonal",.8),("diagonal",1.)):
        report[f"{geometry}_{width}"] = run(geometry,rows,width)
    (OUT/"ranking-cpu.json").write_text(json.dumps(report,indent=2)+"\n",encoding="utf8")
