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


if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("before",type=Path)
    p.add_argument("after",type=Path)
    p.add_argument("output",type=Path)
    args=p.parse_args()
    compare(args.before,args.after,args.output)
