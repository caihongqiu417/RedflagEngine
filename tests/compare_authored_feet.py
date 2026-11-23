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


def compare(archive,folder,check_box):
    take=json.loads((folder/"take.json").read_bytes())
    with archive.open("rb") as file: assert hashlib.file_digest(file,"sha256").hexdigest()==take["archive_sha256"]
    frames={r["seq"]:r for r in map(json.loads,gzip.decompress((folder/"frames.jsonl.gz").read_bytes()).splitlines())}
    with zipfile.ZipFile(archive) as zip:
        scene=yaml.safe_load(zip.read("assets/scenes/Watt-Station.redflag"))
        player=next(e for e in scene["Entities"] if e["Entity"]==1101)
        box=next(e for e in scene["Entities"] if e["Entity"]==1112)
        model=player["ModelRendererComponent"]["FilePath"]
        data=zip.read(model)
    size=struct.unpack_from("<I",data,12)[0]
    rig=Rig(json.loads(data[20:20+size]),data[28+size:])
    box_pose=box["TransformComponent"]
    box_shape=box["BoxCollider3DComponent"]
    box_position=np.array(box_pose["Translation"])[[0,2]]
    box_yaw=box_pose["Rotation"][1]
    sin,cos=math.sin(box_yaw),math.cos(box_yaw)
    # 这个维修站资产只绕竖轴旋转，把脚踝投到箱子自身的长宽上。
    to_box=np.array([[cos,-sin],[sin,cos]])
    box_center=np.array(box_shape["Center"])[[0,2]]
    box_half=np.array(box_shape["HalfSize"])[[0,2]]
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
            sin,cos=math.sin(yaw),math.cos(yaw)
            from_model=np.array([[cos,sin],[-sin,cos]])
            hips=world[rig.names["hips"]][0][i]
            case=dict(seq=seq,clip=name,clip_time=float(times[i]),position=live["position"],yaw=yaw,feet={})
            for side in ("L","R"):
                toe="toe."+side;foot="foot."+side
                authored=world[rig.names[toe]][0][i]
                ankle=world[rig.names[foot]][0][i]
                delta=authored-ankle
                actual=np.array(joints[toe])-np.array(joints[foot])
                height=joints[toe][1]-live["position"][1]
                # 箱子是否在原片该脚正下方。用实机髋部作为原点，可扣除场景运动和根骨的位移。
                ankle_xz=np.array(joints["hips"])[[0,2]]+from_model@(ankle-hips)[[0,2]]
                local_box=(to_box@(ankle_xz-box_position))/np.array(box_pose["Scale"])[[0,2]]-box_center
                case["feet"][side]=dict(authored_toe_y_m=float(authored[1]),game_toe_y_m=height,
                    difference_y_m=height-float(authored[1]), authored_forward_m=float(delta[2]),
                    game_forward_m=float(actual[0]*sin+actual[2]*cos),authored_ankle_xz=ankle_xz.tolist(),
                    toolbox_local_xz=local_box.tolist(),over_toolbox=bool(np.all(np.abs(local_box)<=box_half)))
            cases.append(case)
    result=dict(archive_sha256=take["archive_sha256"],model=model,glb_sha256=hashlib.sha256(data).hexdigest(),
                actor=1101,frames=sorted(cases,key=lambda c:c["seq"]))
    nearby=[dict(seq=f["seq"],side=s,clip=f["clip"],clip_time=f["clip_time"],
                 authored_toe_y_m=w["authored_toe_y_m"],difference_y_m=w["difference_y_m"])
            for f in result["frames"] for s,w in f["feet"].items() if w["over_toolbox"]]
    result["over_toolbox"]=nearby
    (folder/"authored-feet.json").write_text(json.dumps(result,indent=2)+"\n",encoding="utf8")
    if check_box:
        if not nearby: raise AssertionError("本局原片的踝没有经过箱体轮廓内，需要另录同路线。")
        # 若角色整个人踩上箱子，那是另一种地形测试。
        heights=[case["position"][1] for case in result["frames"]]
        assert max(heights)-min(heights)<.01, heights
        # 初版三个样本都比原片高了 29–47 cm。复测以 2 cm 允许量检查，保留每个样本的原始差值。
        assert max(abs(w["difference_y_m"]) for w in nearby)<.02, nearby


if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("archive",type=Path);p.add_argument("folder",type=Path)
    p.add_argument("--verify-toolbox",action="store_true",help="确认实机步伐跨过红箱，鞋尖相对原片的额外高度在2厘米内")
    args=p.parse_args()
    compare(args.archive,args.folder,args.verify_toolbox)
