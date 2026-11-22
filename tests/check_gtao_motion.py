"""箱脚与细柱脚的环境遮蔽。先在仓库根运行 tests/run_screen_effects.cmd --gtao-camera。"""
import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image

WIDTH, HEIGHT = 320, 180
RADIUS = .65


def floor_strip(left, right, front, offsets):
    x, z = np.meshgrid(np.linspace(left, right, 15), front + np.array(offsets))
    return np.stack([x.ravel(), np.zeros(x.size), z.ravel(), np.ones(x.size)], axis=1)


def visibility(points, samples=8192):
    # 余弦分布的半球方向。每条射线的权重已经包含在抽样概率里。
    k = np.arange(samples) + .5
    height = np.sqrt(1 - k/samples)
    angle = k * (np.pi * (3-np.sqrt(5)))
    directions = np.stack((np.sqrt(k/samples)*np.cos(angle), height, np.sqrt(k/samples)*np.sin(angle)), axis=1)
    ranges = [((-.5,0,-.5),(.5,1,.5)), ((.99,0,-.06),(1.11,1,.06))]
    expected = []
    for p in points[:, :3]:
        distance = np.full(samples, np.inf)
        for low, high in ranges:
            a, b = (low-p)/directions, (high-p)/directions
            near, far = np.minimum(a,b).max(1), np.maximum(a,b).min(1)
            hit = (near >= 0) & (far >= near)
            distance[hit] = np.minimum(distance[hit], near[hit])
        # 半径的最后四成按项目曲线减弱遮蔽，两种解法量同一段距离。
        x = ((distance-.6*RADIUS)/(.4*RADIUS)).clip(0,1)
        expected.append((x*x*(3-2*x)).mean())
    return np.array(expected)


def floor_samples(frame, matrix, points, channel):
    h,w=frame.shape[:2]
    clip = points @ matrix.T
    uv = (clip[:,:2]/clip[:,3,None]*[.5,-.5]+.5)*[w,h]-.5
    x, y = np.floor(uv).astype(int).T
    fx, fy = (uv - np.floor(uv)).T
    result = np.zeros(len(points))
    total = np.zeros(len(points))
    for dx, dy in ((0,0),(1,0),(0,1),(1,1)):
        pixel = frame[y+dy,x+dx]
        w = (fx if dx else 1-fx) * (fy if dy else 1-fy) * (abs(pixel[:,1]) < .00002)
        result += pixel[:,channel]*w
        total += w
    return np.divide(result,total,out=np.full_like(result,np.nan),where=total>.5)


def summarize(data, reference):
    # 每一列是同一处地面，必须在全部 13 个视角都可见。
    valid = np.isfinite(data).all(0)
    if valid.mean() < .75:
        raise ValueError(f"可追踪地面点不足 {valid.sum()}/{valid.size}")
    data, target = data[:,valid], reference[valid]
    return dict(points=int(valid.sum()), reference=float(target.mean()), mean=float(data.mean()),
                mae=float(abs(data-target).mean()), frame_delta=float(abs(np.diff(data,axis=0)).mean()),
                peak_to_peak=float(np.ptp(data,axis=0).mean()),
                first_middle_last=data[[0,6,12]].mean(1).tolist())


def report(source, output):
    # 近处包含距箱脚/柱脚 2–10 cm 的同一排地面。另量远处空地，检查整片地面误变暗。
    patches = {
        "box_contact": floor_strip(-.41,.41,.5,[.02,.04,.06,.08,.10]),
        "box_falloff": floor_strip(-.41,.41,.5,[.13,.19,.26,.35,.45,.59]),
        "post_contact": floor_strip(1.0,1.10,.06,[.02,.035,.05,.075,.10]),
        "open_floor": floor_strip(-.5,.5,.5,[.72,.82,.95])}
    expected = {k:visibility(v) for k,v in patches.items()}
    results = {}
    output.mkdir(parents=True, exist_ok=True)
    for mode in ("perspective","taa","ortho"):
        world_to_clip = np.loadtxt(source/mode/"cameras.csv",delimiter=",").reshape(-1,4,4)
        if world_to_clip.shape != (13,4,4): raise ValueError("需要完整的 13 帧平移")
        series = {k:[] for k in patches}
        raw = {k:[] for k in patches}
        reprojection = []
        stable = np.loadtxt(source/"perspective"/"cameras.csv",delimiter=",").reshape(-1,4,4) if mode=="taa" else world_to_clip
        for n, matrix in enumerate(world_to_clip):
            pixels = np.fromfile(source/mode/f"{n}.f32",np.float32).reshape(HEIGHT,WIDTH,5)
            if not np.isfinite(pixels).all(): raise ValueError("屏幕数据含非有限数值")
            for k,p in patches.items():
                # 最终 TAA 图显示在面板的固定像素上。当前帧原始 AO 则来自有亚像素偏移的相机。
                series[k].append(floor_samples(pixels,stable[n],p,4))
                raw[k].append(floor_samples(pixels,matrix,p,3))
            floor = abs(pixels[:,:,1]) < .00002
            yy,xx = np.where(floor)
            clip = np.column_stack((pixels[:,:,:3][floor],np.ones(len(xx)))) @ matrix.T
            raster = (clip[:,:2]/clip[:,3,None]*[.5,-.5]+.5)*[WIDTH,HEIGHT]
            reprojection.extend(np.max(abs(raster-np.column_stack((xx+.5,yy+.5))),axis=1)[::32])
            if n in {0,6,12}:
                gray = np.repeat(np.rint(pixels[:,:,4:5].clip(0,1)*255).astype('uint8'),3,axis=2)
                gray[pixels[:,:,1]==-100] = [137,151,165]
                Image.fromarray(gray).save(output/f"{mode}-{n}.png")
        results[mode] = {k:summarize(np.array(v),expected[k]) for k,v in series.items()}
        results[mode]["max_reprojection_pixels"] = float(max(reprojection))
        if mode=="taa":
            results["jitter_only"] = {k:summarize(np.array(v),expected[k]) for k,v in raw.items()}
    (output/"metrics.json").write_text(json.dumps(results,indent=2)+"\n",encoding="utf8")
    for mode, groups in results.items():
        print(mode,[(k,round(v["mae"],5),round(v["frame_delta"],5)) for k,v in groups.items() if isinstance(v,dict)])
    return results


def compare(current, baseline):
    # 遮蔽图和射线图都使用 0 至 1 的线性可见度。正交和透视下，无遮挡地面都应得到 1。
    for mode in ("perspective","taa","ortho"):
        for patch in ("box_contact","box_falloff","post_contact"):
            if current[mode][patch]["mae"] >= baseline[mode][patch]["mae"]:
                raise ValueError(f"{mode}/{patch} 与射线参考的误差没有降低")
        if current[mode]["open_floor"]["mae"] > 1.e-5 or current[mode]["max_reprojection_pixels"] >= .01:
            raise ValueError(f"{mode} 空地可见度或像素坐标有误")
    for patch in ("box_contact","post_contact"):
        if current["taa"][patch]["frame_delta"] >= baseline["taa"][patch]["frame_delta"]:
            raise ValueError(f"移动相机时 {patch} 的逐帧差增大")


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input",type=Path,default=Path("tmp/gtao-camera"))
    parser.add_argument("--output",type=Path,default=Path("output/validation/gtao-motion"))
    parser.add_argument("--compare",type=Path,help="读入旧版 metrics.json，对照同一摄像机轨迹")
    args = parser.parse_args()
    current = report(args.input,args.output)
    if args.compare: compare(current,json.loads(args.compare.read_text(encoding="utf8")))
