"""用发行包原始 glTF 剪辑在同一时刻采样，与游戏已经保存的最终姿势对照。"""
import argparse
import gzip
import hashlib
import json
import math
from pathlib import Path
import struct
import sys
import zipfile
import numpy as np
import yaml

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"scripts"))
from retarget_station_motion import Rig


def compare(archive,folder):
    take=json.loads((folder/"take.json").read_bytes())
    with archive.open("rb") as file: assert hashlib.file_digest(file,"sha256").hexdigest()==take["archive_sha256"]
    frames={r["seq"]:r for r in map(json.loads,gzip.decompress((folder/"frames.jsonl.gz").read_bytes()).splitlines())}
    with zipfile.ZipFile(archive) as zip:
        scene=yaml.safe_load(zip.read("assets/scenes/Watt-Station.redflag"))
        player=next(e for e in scene["Entities"] if e["Entity"]==1101)
        model=player["ModelRendererComponent"]["FilePath"]
        data=zip.read(model)
    size=struct.unpack_from("<I",data,12)[0]
    rig=Rig(json.loads(data[20:20+size]),data[28+size:])
    phases={}
    for c in take["inputs"]: phases.setdefault(c["phase"],c["applied_seq"])
    cases=[]
    for name in ("walking_gun-cycle","running_gun-cycle"):
        group=[]
        for seq in range(phases["walk"],phases["stop"]):
            live=next(a for a in frames[seq]["actors"] if a["id"]=="1101")
            if live["animation"]["clip"]==name:group.append((seq,live))
        times=np.array([a["animation"]["time"] for _,a in group])
        world=rig.world(rig.pose(rig.clip(name),times))
        for i,(seq,live) in enumerate(group):
            anim=live["animation"];yaw=anim["yaw"]; joints=anim["nodes"]
            case=dict(seq=seq,clip=name,clip_time=float(times[i]),feet={})
            for side in ("L","R"):
                toe="toe."+side;foot="foot."+side
                authored=world[rig.names[toe]][0][i]
                ankle=world[rig.names[foot]][0][i]
                delta=authored-ankle
                actual=np.array(joints[toe])-np.array(joints[foot])
                height=joints[toe][1]-live["position"][1]
                case["feet"][side]=dict(authored_toe_y_m=float(authored[1]),game_toe_y_m=height,
                    difference_y_m=height-float(authored[1]), authored_forward_m=float(delta[2]),
                    game_forward_m=float(actual[0]*math.sin(yaw)+actual[2]*math.cos(yaw)))
            cases.append(case)
    result=dict(archive_sha256=take["archive_sha256"],model=model,glb_sha256=hashlib.sha256(data).hexdigest(),
                actor=1101,frames=sorted(cases,key=lambda c:c["seq"]))
    (folder/"authored-feet.json").write_text(json.dumps(result,indent=2)+"\n",encoding="utf8")


if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("archive",type=Path);p.add_argument("folder",type=Path)
    args=p.parse_args()
    compare(args.archive,args.folder)
