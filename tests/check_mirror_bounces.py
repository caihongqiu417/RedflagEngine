"""在固定相机与两面成直角的镜子之间追踪灯牌。运行目录为仓库根目录。"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import sys

import numpy as np
from PIL import Image
import yaml

ROOT=Path(__file__).resolve().parents[1]
WIDTH=480


def make_scene(path, bounces, emission, block=False, tint=False, mirrors=2):
    camera=yaml.safe_load((ROOT/"RedflagForge/assets/scenes/Mirror-Studio.redflag").read_text(encoding="utf8"))["Entities"][0]
    camera["TransformComponent"].update(Translation=[0,0,3],Rotation=[0,0,0])
    camera["CameraComponent"]["Camera"]["PerspectiveFov"]=.67
    entities=[camera]

    def box(name,where,scale,yaw,color,metal=0,roughness=.65,glow=None):
        mat=dict(Color=[1,1,1,1],BaseColor=[*color,1],Metallic=metal,Roughness=roughness)
        if glow: mat["Emission"]=glow
        entities.append(dict(Entity=len(entities)+1,TagComponent=dict(Tag=name),
            TransformComponent=dict(Translation=where,Scale=scale,Rotation=[0,yaw,0]),MeshRendererComponent=mat))

    box("第一面镜子",[0,0,0],[1.6,1.6,.025],math.pi/4,[1,1,1],1,0)
    box("第二面镜子",[2,0,0],[1.6,1.6,.025],-math.pi/4,[.25,1,.5] if tint else [1,1,1],1,0)
    box("镜后墙面",[0,0,-1.6],[9,4,.1],0,[.26,.33,.40])
    box("地面",[0,-.95,.5],[9,.1,9],0,[.20,.23,.26])
    sign=[2,0,2]; yaw=0.
    if mirrors>=3:
        box("第三面镜子",[2,0,2],[1.6,1.6,.025],-math.pi/4,[1,1,1],1,0)
        sign=[4,0,2]; yaw=math.pi/2
    if mirrors==4:
        box("第四面镜子",[4,0,2],[1.6,1.6,.025],math.pi/4,[1,1,1],1,0)
        sign=[4,0,0]; yaw=0.
    box("彩色灯牌",sign,[.95,.48,.025],yaw,[0,0,0],1,0,emission)
    box("白色短灯条",[sign[0],.37,sign[2]],[.54,.07,.025],yaw,[0,0,0],1,0,[1.4,1.4,1.4])
    if block: box("挡板",[2,0,1],[1.38,1.22,.06],0,[0,0,0],1,0)
    entities.append(dict(Entity=20,TagComponent=dict(Tag="照亮地面的方向光"),
        TransformComponent=dict(Translation=[0,0,0],Rotation=[0,0,0],Scale=[1,1,1]),
        DirectionalLightComponent=dict(Direction=[-.1,-.85,-.45],Color=[1,1,1],Intensity=1,CastShadow=True)))
    path.write_text(yaml.safe_dump(dict(Scene=path.stem,Entities=entities),allow_unicode=True,sort_keys=False),encoding="utf8")
    settings=dict(Shading="Lit",SSR=True,SceneReflections=True,ReflectionBounces=bounces,ReflectionDistance=12,
        Direct="All point lights",Indirect="Off",GTAO=False,SpecularIBL=False,TAA=True,HistoryWeight=0.,
        ToneMapping="ACES",ExposureEV=0.,PreviewWidth=WIDTH)
    Path(str(path)+".render.yaml").write_text(yaml.safe_dump(settings),encoding="utf8")


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe",type=Path,default=ROOT/"bin/Debug-windows-x86_64/RedflagForge/RedflagForge.exe")
    parser.add_argument("--renderdoc",type=Path,default=ROOT/"tmp/tools/renderdoc/RenderDoc_1.47_64")
    parser.add_argument("--reuse",action="store_true")
    parser.add_argument("--cases",nargs="+")
    parser.add_argument("--output",type=Path,default=ROOT/"output/validation/mirror-bounces")
    args=parser.parse_args()
    exe=args.exe.resolve(strict=True)
    version=hashlib.sha256(exe.read_bytes()).hexdigest()
    cases=dict(one=(1,[3.2,.24,.06]),two=(2,[3.2,.24,.06]),blue=(2,[.08,.4,2.4]),
        dark=(2,[0,0,0]),blocked=(2,[3.2,.24,.06],True),tinted=(2,[3.2,.24,.06],False,True))
    for key,count in (("three",3),("four",4)):
        cases[key]=(count,[.18,2.1,.57],False,False,count)
        cases[key+"-short"]=(count-1,[.18,2.1,.57],False,False,count)
    out=ROOT/"tmp/mirror-bounces"
    out.mkdir(parents=True,exist_ok=True)
    args.output.mkdir(parents=True,exist_ok=True)
    for key in args.cases or cases:
        scene=out/("mirrors-"+key+".redflag")
        make_scene(scene,*cases[key])
        work=ROOT/"tmp/material-reference"/f"{scene.stem}-{version[:12]}-{WIDTH}-f8"
        if args.reuse and (work/"replay.json").is_file():
            entry=work/"assets/scenes/Sponza-Atrium.redflag"
            assert yaml.safe_load(scene.read_text(encoding="utf8"))==yaml.safe_load(entry.read_text(encoding="utf8"))
            assert yaml.safe_load(Path(str(scene)+".render.yaml").read_text())==yaml.safe_load(Path(str(entry)+".render.yaml").read_text())
        else:
            env=dict(os.environ,RF_MEASURE_REFLECTIONS="1")
            subprocess.run([sys.executable,str(ROOT/"tests/capture_material_studio.py"),"--exe",str(exe),
                "--renderdoc",str(args.renderdoc),"--scene",str(scene),"--width",str(WIDTH),"--frame","8"],env=env,check=True)
        general=json.loads((work/"replay.json").read_text())
        width,height=general["size"]
        center=np.s_[height//2-2:height//2+3,width//2-2:width//2+3,:3]
        rgb=lambda f: np.fromfile(work/f,np.float32).reshape(height,width,4)
        source,result=rgb("reflection-in.f32"),rgb("reflection-out.f32")
        frame=Path((work/"frame-path.txt").read_text())
        row=dict(build=version,capture=frame.relative_to(ROOT).as_posix(),sha256=hashlib.file_digest(open(frame,"rb"),"sha256").hexdigest(),
            renderdoc=general["tool"],frame=general["frame"],size=[width,height],camera_draws=len(general["draws"]),
            input_rgb=source[center].mean((0,1)).tolist(),output_rgb=result[center].mean((0,1)).tolist(),reflection=general["reflection"])
        (args.output/(key+".json")).write_text(json.dumps(row,indent=2)+"\n",encoding="utf8")
        image=np.rint(np.clip(rgb("display.f32")[:,:,:3],0,1)*255).astype(np.uint8)
        Image.fromarray(image).save(args.output/(key+".png"))
        print(key,row["input_rgb"],row["output_rgb"],flush=True)

    if args.cases: return
    outputs={key:np.array(json.loads((args.output/(key+".json")).read_text())["output_rgb"]) for key in cases}
    before=json.loads((args.output/"one.json").read_text())
    after=json.loads((args.output/"two.json").read_text())
    assert before["input_rgb"]==after["input_rgb"]
    assert max(abs(outputs["two"]-outputs["dark"]-np.array([3.2,.24,.06])))<.005, outputs
    assert max(abs(outputs["blue"]-outputs["dark"]-np.array([.08,.4,2.4])))<.005
    assert max(abs(outputs["one"]-outputs["dark"]))<.005 and max(abs(outputs["blocked"]-outputs["dark"]))<.005
    assert .23 < outputs["tinted"][0]/outputs["two"][0] < .27
    assert after["reflection"]["bounces"]==2
    for key in ("three","four"):
        assert max(abs(outputs[key]-outputs[key+"-short"]-np.array([.18,2.1,.57])))<.005


if __name__=="__main__": main()
