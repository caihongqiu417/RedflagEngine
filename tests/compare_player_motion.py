"""按游戏实际收到按键的时刻对齐两个发行包的实机视频，输出上下两行逐帧对照。"""
import argparse
import bisect
import json
from pathlib import Path
import subprocess
import tempfile
from PIL import Image, ImageDraw


def read(folder):
    take = json.loads((folder / "take.json").read_bytes())
    frames = {r["seq"]:r for r in (json.loads(line) for line in (folder/"frames.jsonl").read_bytes().splitlines())}
    starts = {}
    for item in take["inputs"]:
        starts.setdefault(item["phase"], frames[item["applied_seq"]]["seconds"] - take["origin"])
    return take["captures"], starts


def compare(before, after, output):
    output.parent.mkdir(parents=True, exist_ok=True)
    recordings = [read(folder) for folder in (before, after)]
    # 两局各自以按键生效帧为零点，同一实拍可能在相邻的 24 Hz 显示帧中停留。
    segments = [("punch",-.15,.82),("punch-repeat",-.12,.80),("walk",.01,.96),
                ("run",.01,.70),("turn",.01,.53),("jump",-.12,.90),("crouch",-.1,.58)]
    with tempfile.TemporaryDirectory() as tmp:
        n = 0
        for phase, start, end in segments:
            for offset in range(round(start*24),round(end*24)):
                sheet = Image.new("RGB",(1024,596),"#161b1f")
                draw = ImageDraw.Draw(sheet)
                for i,(source,(captures,inputs)) in enumerate(zip((before,after),recordings)):
                    seconds = [f["elapsed"] for f in captures]
                    t = inputs[phase]+offset/24
                    right = min(bisect.bisect_left(seconds,t),len(captures)-1)
                    idx = min((right,max(0,right-1)),key=lambda j:abs(seconds[j]-t))
                    cap = captures[idx]
                    with Image.open(source/"images"/f"{cap['seq']:05}.jpg") as picture:
                        crop = picture.crop((225,205,768,580))
                        crop.thumbnail((512,265))
                        sheet.paste(crop,((512-crop.width)//2,i*298))
                        full = picture.resize((500,281))
                        sheet.paste(full,(518,i*298))
                    title = ("Before","After")[i]+f"    {phase}    {seconds[idx]-inputs[phase]:+.2f}s    #{cap['seq']}"
                    draw.text((12,i*298+271),title,fill="white")
                sheet.save(Path(tmp)/f"{n:04}.jpg", quality=95)
                n+=1
        subprocess.run(["ffmpeg","-hide_banner","-loglevel","error","-y","-framerate","24",
                        "-i",str(Path(tmp)/"%04d.jpg"),"-c:v","libx264","-crf","18","-pix_fmt","yuv420p",
                        "-movflags","+faststart",str(output)],check=True)


def foot_sheet(before, after, reports, output):
    first,second=[json.loads(path.read_bytes())["over_toolbox"] for path in reports]
    # 在相同侧脚、相同剪辑且原片脚踝都位于箱子上方的样本中，选取不重复、时间最接近的配对。
    pairs=[]
    for side in ("L","R"):
        for clip in sorted({s["clip"] for s in first}):
            a=[s for s in first if s["side"]==side and s["clip"]==clip]
            b=[s for s in second if s["side"]==side and s["clip"]==clip]
            if not a or not b: continue
            # 一次摆步只有少量取样；下面优先选时差最小的配对。
            choices=sorted((abs(x["clip_time"]-y["clip_time"]),x["seq"],y["seq"],x,y) for x in a for y in b)
            used_a,used_b=set(),set()
            for _,i,j,x,y in choices:
                if i not in used_a and j not in used_b:
                    pairs.append((x,y));used_a.add(i);used_b.add(j)
    if not pairs: raise ValueError("两次录像没有相同侧脚和剪辑经过箱子")
    pairs.sort(key=lambda p:p[0]["clip_time"])
    pairs=pairs[:6]
    sheet=Image.new("RGB",(326*len(pairs),552),"#182027");pen=ImageDraw.Draw(sheet)
    for column,pair in enumerate(pairs):
        for row,(src,s) in enumerate(zip((before,after),pair)):
            take=json.loads((src/"take.json").read_bytes())
            cap=min(take["captures"],key=lambda c:abs(c["seq"]-s["seq"]))
            with Image.open(src/"images"/f"{cap['seq']:05}.jpg") as picture:
                crop=picture.crop((422,237,739,585))
                crop.thumbnail((326,230))
                x=column*326;y=row*276
                sheet.paste(crop,(x+(326-crop.width)//2,y))
                label=("Before","After")[row]
                pen.text((x+9,y+234),f"{label}   sample #{s['seq']}   image #{cap['seq']}   clip {s['clip_time']:.3f}s",fill="white")
                delta=s["difference_y_m"]*100
                shown="within 0.1 cm" if abs(delta)<.1 else f"{delta:+.1f} cm"
                pen.text((x+9,y+251),"Toe height vs. source: "+shown,fill="white")
    output.parent.mkdir(parents=True,exist_ok=True)
    sheet.save(output,quality=95)


if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("before",type=Path)
    p.add_argument("after",type=Path)
    p.add_argument("output",type=Path)
    p.add_argument("--toolbox",type=Path,nargs=2,metavar=("BEFORE_JSON","AFTER_JSON"),
                   help="按实际脚下有箱体的剪辑时刻对齐，而后生成两排足部对照图")
    args=p.parse_args()
    if args.toolbox: foot_sheet(args.before,args.after,args.toolbox,args.output)
    else: compare(args.before,args.after,args.output)
