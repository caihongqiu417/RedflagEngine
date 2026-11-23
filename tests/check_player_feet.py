"""读取已保存的游戏骨骼，比较行走、奔跑与待机时脚踝到脚趾的方向。"""
import argparse
import gzip
import json
import math
from pathlib import Path
import subprocess
import numpy as np
from PIL import Image, ImageDraw


def point(actor, side):
    pose = actor["animation"]
    ankle = np.array(pose["nodes"]["foot."+side])
    toe = np.array(pose["nodes"]["toe."+side])
    x,y,z = toe-ankle
    sin,cos = math.sin(pose["yaw"]),math.cos(pose["yaw"])
    return np.array([x*cos-z*sin,y,x*sin+z*cos]), ankle[1]-actor["position"][1], toe[1]-actor["position"][1]


def spans(rows, flag):
    result = []
    for i,r in enumerate(rows):
        if not r[flag]: continue
        if i and rows[i-1][flag] and r["seq"]==rows[i-1]["seq"]+1:
            result[-1]["last"] = r["seq"]
            result[-1]["last_s"] = r["seconds"]
        else:
            result.append(dict(first=r["seq"],last=r["seq"],first_s=r["seconds"],last_s=r["seconds"]))
    return result


def draw(folder, raw, take, report):
    running = report["actors"]["1101"]
    samples={s["seq"]:{"seconds":s["seconds"],"clip_time":s["clip_time"],"left":s["backward"]}
             for phase in ("run","turn") for s in running["L"][phase]["samples"]}
    for phase in ("run","turn"):
        for s in running["R"][phase]["samples"]: samples[s["seq"]]["right"]=s["backward"]
    drawn=[number for number,s in samples.items() if s["left"] or s["right"]]
    if not drawn: return
    pictures = [cap for cap in take["captures"] if drawn[0]<=cap["seq"]<=drawn[-1]+1 and cap["seq"] in samples]
    sheet=Image.new("RGB",(4*290, math.ceil(len(pictures)/4)*228),"#182027")
    pen=ImageDraw.Draw(sheet)
    for i,cap in enumerate(pictures):
        seq=cap["seq"]
        s=samples[seq]
        with Image.open(raw/"images"/f"{seq:05}.jpg") as frame:
            crop=frame.crop((425,275,763,604))
            crop.thumbnail((290,191))
            x,y=(i%4)*290,(i//4)*228
            sheet.paste(crop,(x+(290-crop.width)//2,y))
            label="L" if s["left"] else "R" if s["right"] else "-"
            pen.text((x+10,y+197),f"#{seq}    clip {s['clip_time']:.2f}s    backwards: {label}",fill="white")
    sheet.save(folder/"feet.jpg",quality=94)
    anchor=samples[drawn[0]]["seconds"]-take["origin"]-.25
    length=samples[drawn[-1]]["seconds"]-samples[drawn[0]]["seconds"]+.6
    subprocess.run(["ffmpeg","-hide_banner","-loglevel","error","-y","-ss",str(max(0,anchor)),
        "-i",str(folder/"game.mp4"),"-t",str(length),"-vf","crop=640:490:270:160","-fps_mode","vfr",
        "-c:v","libx264","-crf","16","-movflags","+faststart",str(folder/"feet.mp4")],check=True)


def check(folder, raw):
    take = json.loads((folder/"take.json").read_bytes())
    rows = [json.loads(line) for line in gzip.decompress((folder/"frames.jsonl.gz").read_bytes()).splitlines()]
    phase = {}
    for command in take["inputs"]: phase.setdefault(command["phase"],command["applied_seq"])
    by_seq = {r["seq"]:r for r in rows}
    def actor(seq,who):
        return next(a for a in by_seq[seq]["actors"] if a["id"]==who)

    report = dict(exe_sha256=take["exe_sha256"],archive_sha256=take["archive_sha256"],
                  replaced_native=take["replaced_native"],actors={})
    for who,segments in (("1101",(("walk","walk","run"),("run","run","turn"),("turn","turn","stop"))),
                         ("1102",(("follow","walk","jump"),))):
        measurements = {}
        for side in ("L","R"):
            rest = [point(actor(n,who),side) for n in range(phase["orbit"]+3,phase["punch"])
                    if actor(n,who)["animation"]["clip"]=="Idle-cycle"]
            rest_vector = np.median([r[0] for r in rest],axis=0)
            rest_y = np.median([r[1:] for r in rest],axis=0)
            facing = rest_vector[[0,2]]/np.linalg.norm(rest_vector[[0,2]])
            rest_angle = math.degrees(math.atan2(rest_vector[1],np.linalg.norm(rest_vector[[0,2]])))
            stats = dict(rest_vec=rest_vector.tolist(),rest_pitch_deg=rest_angle,rest_ankle_toe_height_m=rest_y.tolist())
            for name,first,last in segments:
                samples=[]
                for n in range(phase[first],phase[last]):
                    a = actor(n,who)
                    vec,ankle_y,toe_y=point(a,side)
                    nodes=a["animation"]["nodes"]
                    hip,knee,ankle = [np.array(nodes[j+"."+side]) for j in ("thigh","shin","foot")]
                    thigh=hip-knee
                    shin=ankle-knee
                    joint_angle=np.clip(np.dot(thigh,shin)/np.linalg.norm(thigh)/np.linalg.norm(shin),-1,1)
                    ahead=float(np.dot(vec[[0,2]],facing))
                    # 以待机脚掌的水平朝向为正。脚趾低于踝且已转向后方时，角度越过 -90°。
                    angle=math.degrees(math.atan2(vec[1],ahead))
                    change=(angle-rest_angle+180)%360-180
                    toe_low = toe_y <= rest_y[1]+.03
                    ankle_low = ankle_y <= rest_y[0]+.03
                    contact = ("low" if ankle_low else "toe_only") if toe_low else ("swing" if not ankle_low else "heel_low")
                    # 近乎竖直时，极短的地面投影不适合辨别脚朝前还是朝后。给前后方向留约 2 cm。
                    backward = ahead < -.10*np.linalg.norm(rest_vector)
                    samples.append(dict(seq=n,seconds=by_seq[n]["seconds"],clip=a["animation"]["clip"],
                        clip_time=a["animation"]["time"],forward_m=ahead,ankle_height_m=ankle_y,toe_height_m=toe_y,
                        pitch_deg=angle,change_from_idle_deg=change,knee_flex_deg=180-math.degrees(math.acos(joint_angle)),phase=contact,
                        backward=bool(backward),toe_down=bool(change < -45)))
                stats[name]=dict(backward_spans=spans(samples,"backward"),toe_down_spans=spans(samples,"toe_down"),
                    backward_by_phase={k:sum(s["phase"]==k and s["backward"] for s in samples)
                                       for k in ("low","toe_only","swing","heel_low")},samples=samples)
            measurements[side] = stats
        report["actors"][who]=measurements
    (folder/"feet.json").write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n",encoding="utf8")
    if raw: draw(folder,raw,take,report)


if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("folder",type=Path)
    p.add_argument("--take",type=Path,help="从原始窗口图生成对应的连续图与短片")
    args=p.parse_args()
    check(args.folder,args.take)
