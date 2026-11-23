"""整理 capture_player_motion.py 的逐帧记录和真实窗口图。--require-animation 用于检查新发行包。"""
import argparse
import bisect
import gzip
import json
import math
from pathlib import Path
import subprocess
import tempfile
import numpy as np
from PIL import Image, ImageDraw

ACTOR = {"player": "1101", "watt": "1102"}


def review(take_dir, output, require_animation):
    output.mkdir(parents=True, exist_ok=True)
    take = json.loads((take_dir / "take.json").read_bytes())
    frames = [json.loads(row) for row in (take_dir / "frames.jsonl").read_bytes().splitlines()]
    by_seq = {r["seq"]: r for r in frames}
    contacts = take["inputs"]
    # 原始每帧含双脚和双拳，按原字节压缩；即使一个观察值未列入摘要，也能另行核对。
    (output / "frames.jsonl.gz").write_bytes(gzip.compress((take_dir / "frames.jsonl").read_bytes(),mtime=0))
    (output / "take.json").write_bytes((take_dir / "take.json").read_bytes())
    times = {c["phase"]: by_seq[c["applied_seq"]]["seconds"] for c in reversed(contacts)}
    take_images = take["captures"]
    secs = [i["elapsed"] + take["origin"] for i in take_images]
    def nearest(t):
        right = min(bisect.bisect_left(secs, t), len(secs)-1)
        idx = min((max(0, right-1), right), key=lambda i: abs(secs[i]-t))
        capture = take_images[idx]
        return capture, take_dir / "images" / f"{capture['seq']:05}.jpg"

    def track(actor, start, stop):
        return [(row["seconds"], next(a for a in row["actors"] if a["id"] == ACTOR[actor]))
                for row in frames if start <= row["seconds"] < stop]

    def gait(actor, start, stop):
        sample = track(actor, start, stop)
        t = np.array([s[0] for s in sample])
        p = np.array([a["position"] for _, a in sample])
        path = np.linalg.norm(np.diff(p[:, [0, 2]], axis=0), axis=1).sum()
        result = dict(samples=len(t), duration_s=t[-1]-t[0], travelled_m=path, speed_m_s=path/(t[-1]-t[0]),
                      start_xyz=p[0].tolist(), end_xyz=p[-1].tolist())
        if not all("animation" in a for _, a in sample): return result

        # 在角色朝向上量前后脚的相对距离。一起向前平移的僵硬模型不会产生换脚。
        clips = [a["animation"] for _, a in sample]
        result["clips"] = list(dict.fromkeys(c["clip"] for c in clips))
        yaw = np.array([c["yaw"] for c in clips])
        result["heading_change_deg"] = math.degrees(float(np.unwrap(yaw)[-1] - np.unwrap(yaw)[0]))
        feet = [np.array([c["nodes"]["foot."+side] for c in clips]) for side in ("L", "R")]
        stride = (feet[0]-feet[1])[:, 0]*np.sin(yaw) + (feet[0]-feet[1])[:, 2]*np.cos(yaw)
        result["feet_forward_separation_m"] = [float(np.min(stride)), float(np.max(stride))]
        result["foot_height_range_m"] = [float(np.ptp(foot[:, 1])) for foot in feet]
        # 这段是在平地录制。脚尖在相邻两帧都处于接近最低点的 3 cm 内时，量它沿地面移动多少。
        # 每侧分别给出实际取到的间隔数。某只脚尚未落地时不会把空样本当作零滑动。
        result["low_toe"] = {}
        for side in ("L", "R"):
            toe = np.array([c["nodes"]["toe."+side] for c in clips])
            low = toe[:,1] <= np.min(toe[:,1])+.03
            used = low[1:] & low[:-1]
            distances = np.linalg.norm(np.diff(toe[:,[0,2]],axis=0),axis=1)[used]
            result["low_toe"][side] = dict(intervals=int(used.sum()),
                horizontal_m=distances.tolist(), seconds=np.diff(t)[used].tolist())
        return result

    walk = gait("player", times["walk"], times["run"])
    run = gait("player", times["run"], times["turn"])
    turn = gait("player", times["turn"], times["stop"])
    watt = gait("watt", times["walk"], times["jump"])
    jump = track("player", times["jump"]-.25, times["crouch"])
    jump_height = float(np.ptp([a["position"][1] for _, a in jump]))
    report = dict(archive_sha256=take["archive_sha256"], exe_sha256=take["exe_sha256"],
        replaced_native=take["replaced_native"], gameplay_sha256=take["gameplay_sha256"],
        images=len(take_images), first_image=take_images[0]["seq"], last_image=take_images[-1]["seq"],
        max_screenshot_readback_frames=max(c["seq_end"]-c["seq"] for c in take_images),
        walk=walk, run=run, turn=turn, watt_follow=watt, jump_height_m=jump_height)

    punches = []
    for name in ("punch", "punch-repeat"):
        t = times[name]
        sample = track("player", t-.25, t+1.04)
        baseline = np.array(Image.open(nearest(t-.12)[1]), dtype=np.int16)
        count = []
        for i, path in (nearest(t+offset) for offset in np.arange(0, .85, .045)):
            actual = np.array(Image.open(path), dtype=np.int16)
            # 固定侧面机位。矩形包括蓝色角色的上臂、前臂及手前方，黄色瓦特站在矩形外。
            delta = np.abs(actual-baseline).max(axis=2)[300:510, 455:646]
            count.append(dict(seq=i["seq"], changed_px=int((delta >= 18).sum())))
        note = dict(applied_seq=next(c["applied_seq"] for c in contacts if c["phase"]==name),
                    peak_visible_change_px=max(v["changed_px"] for v in count), changed=count)
        if all("animation" in actor for _,actor in sample):
            observed = [a["animation"] for _,a in sample]
            note["clips"] = list(dict.fromkeys(s["clip"] for s in observed))
            note["clip_samples"] = [dict(offset_s=ts-t, clip=s["clip"], clip_time=s["time"], yaw=s["yaw"])
                                    for (ts,_),s in zip(sample,observed)]
            note["arms"] = {}
            for side in ("L", "R"):
                # 投影到演员的前向，去掉髋的平移；同时量实际肩到拳的伸展和收回。
                forward = []
                extension = []
                for s in observed:
                    nodes = s["nodes"]
                    yaw = s["yaw"]
                    fist = np.array(nodes["f_middle.01."+side])
                    shoulder = np.array(nodes["upper_arm."+side])
                    local = fist-np.array(nodes["hips"])
                    forward.append(float(local[0]*math.sin(yaw)+local[2]*math.cos(yaw)))
                    extension.append(float(np.linalg.norm(fist-shoulder)))
                pre = np.array([s[0]<t for s in sample])
                distance = np.array(forward)-np.median(np.array(forward)[pre])
                note["arms"][side] = dict(peak_forward_m=float(max(distance)),
                    end_forward_m=float(distance[-1]), retract_from_peak_m=float(max(distance)-distance[-1]),
                    shoulder_fist_range_m=[min(extension), max(extension)])
        punches.append(note)
    report["punches"] = punches
    assert walk["travelled_m"]>1.1 and run["travelled_m"]>1.2 and turn["travelled_m"]>0.55
    assert watt["travelled_m"]>1. and jump_height>.20
    if require_animation:
        assert "clips" in walk and "clips" in run and "clips" in watt, "缺少实时骨骼"
        for kind in (walk, run, watt):
            lo, hi = kind["feet_forward_separation_m"]
            assert lo < -.04 and hi > .04, kind
        for hit in punches:
            assert any("punch" in clip.lower() for clip in hit["clips"]) and hit["clip_samples"][-1]["clip"]=="Idle-cycle", hit
            arm = max(hit["arms"].values(), key=lambda a: a["peak_forward_m"])
            # 待机本身会抬腕。要求收回 80% 的前伸量，最后回到待机剪辑。
            assert arm["peak_forward_m"] > .12 and arm["retract_from_peak_m"] > .8 * arm["peak_forward_m"], arm
        for name in ("punch", "punch-repeat"):
            t = times[name]
            events = [event["kind"] for frame in frames if t-.01 <= frame["seconds"] < t+1.04
                      for event in frame["events"] if event["entity"] in ("1101",1101)]
            assert [e for e in events if e.startswith("punch_")] in (
                ["punch_begin","punch_miss","punch_end"], ["punch_begin","punch_hit","punch_end"]), events
        turn_delta = abs(turn["heading_change_deg"])
        assert 35 < turn_delta < 135, turn

    (output / "measured.json").write_text(json.dumps(report, ensure_ascii=False, indent=2)+"\n", encoding="utf8")
    def sheet(name, anchors, crop):
        target = Image.new("RGB", (4*280, math.ceil(len(anchors)/4)*214), "#14191d")
        draw = ImageDraw.Draw(target)
        for i, (label, t) in enumerate(anchors):
            cap, path = nearest(t)
            img = Image.open(path).crop(crop)
            img.thumbnail((280, 187))
            x, y = (i%4)*280, (i//4)*214
            target.paste(img, (x+(280-img.width)//2, y))
            draw.text((x+6, y+190), f"{label}   #{cap['seq']}   {cap['elapsed']:.2f}s", fill="white")
        target.save(output / (name+".jpg"), quality=94)
    side = take.get("profile_second_punch", False)
    moment = times["punch-repeat"] if side else times["punch"]
    sheet("punch", [("F", moment+t) for t in (-.10,.06,.14,.21,.28,.36,.46,.64)], (355,210,765,600))
    sheet("gait", [(phase, times[phase]+offset) for phase, offsets in
          (("walk",(.03,.30,.58,.84)), ("run",(.07,.24,.43,.62)), ("turn",(.02,.15,.33,.52)))
          for offset in offsets], (235,245,780,614))

    # 游戏在每台机器的帧间隔不同。每张照片按原记录显示到下一张，不插帧或加速。
    with tempfile.TemporaryDirectory() as temporary:
        playlist = Path(temporary) / "frames.ffconcat"
        lines = ["ffconcat version 1.0"]
        for n, cap in enumerate(take_images):
            p = (take_dir / "images" / f"{cap['seq']:05}.jpg").resolve().as_posix()
            quoted = p.replace("'", "'\\''")
            lines += ["file '"+quoted+"'", "option framerate 1000"]
            if n+1 < len(take_images):
                lines += [f"duration {take_images[n+1]['elapsed']-cap['elapsed']:.6f}"]
        playlist.write_text("\n".join(lines)+"\n", encoding="utf8")
        subprocess.run(["ffmpeg","-hide_banner","-loglevel","error","-y","-f","concat","-safe","0",
            "-i",str(playlist),"-fps_mode","vfr","-pix_fmt","yuv420p","-c:v","libx264","-crf","19",
            "-movflags","+faststart",str(output / "game.mp4")], check=True)
        if side:
            subprocess.run(["ffmpeg","-hide_banner","-loglevel","error","-y","-ss",f"{moment-take['origin']-.15:.6f}",
                "-i",str(output/"game.mp4"),"-t","1.22","-vf","crop=650:490:240:140","-fps_mode","vfr",
                "-c:v","libx264","-crf","16","-movflags","+faststart",str(output/"punch-side.mp4")],check=True)
    print(json.dumps({k:report[k] for k in ("images","walk","run","watt_follow","jump_height_m")},ensure_ascii=False))


if __name__=="__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("take",type=Path)
    p.add_argument("output",type=Path)
    p.add_argument("--require-animation", action="store_true")
    a = p.parse_args()
    review(a.take,a.output,a.require_animation)
