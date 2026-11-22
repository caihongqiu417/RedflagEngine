"""用相同相机拍下角色移动前后的阴影。运行目录为仓库根目录。"""
import argparse
import hashlib
import json
import os
from pathlib import Path
from statistics import median
import subprocess
import sys

import numpy as np
from PIL import Image
import yaml

from capture_material_studio import run_renderdoc
from profile_native_navigation import prepare

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "output/validation/animated-shadows"


def fixture(path, x, shadows, cast, direct, moving):
    source = ROOT / "RedflagForge/assets/scenes/Character-Walk.redflag"
    scene = yaml.safe_load(source.read_text(encoding="utf8"))
    camera, actor, floor, lamp = scene["Entities"]
    camera["TransformComponent"].update(Translation=[0, 2.6, 5.8], Rotation=[-.30, 0, 0])
    actor.pop("SkeletonAnimationComponent")
    actor["TransformComponent"]["Translation"] = [x, 0, 0]
    actor["ModelRendererComponent"] = dict(FilePath="assets/game/station/crew/Operator.glb", Color=[1,1,1,1])
    actor["ModelAnimationComponent"] = dict(Clip="Idle-cycle", Time=.2, Playing=moving)
    floor["TransformComponent"]["Scale"] = [12,.12,7]
    floor["MeshRendererComponent"]["BaseColor"] = [.6,.6,.6,1.]
    lamp.pop("PointLightComponent")
    lamp["DirectionalLightComponent"] = dict(Direction=[.68,-1,-.45],Color=[1,.94,.85],Intensity=2.3,CastShadow=cast)
    scene.update(Scene=path.stem, Environment="../textures/sky/daylight.hdr", SkyVisible=True)
    path.write_text(yaml.safe_dump(scene, allow_unicode=True, sort_keys=False), encoding="utf8")
    # 历史权重为零保留当前帧。打开 TAA 让静止的编辑器也能在指定帧留下完整的光照命令。
    settings = dict(Shading="Lit", TAA=True, HistoryWeight=0., ToneMapping="ACES", ExposureEV=-.5,
                    Direct=direct, Indirect="Diffuse IBL", SpecularIBL=True, GTAO=False, SSR=False,
                    PreviewWidth=600, AnimatedShadowSize=shadows)
    Path(str(path)+".render.yaml").write_text(yaml.safe_dump(settings), encoding="utf8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=ROOT/"bin/Debug-windows-x86_64/RedflagForge/RedflagForge.exe")
    parser.add_argument("--renderdoc", type=Path, default=ROOT/"tmp/tools/renderdoc/RenderDoc_1.47_64")
    parser.add_argument("--cases", nargs="+", help="只截指定的对照")
    parser.add_argument("--reuse", action="store_true", help="回放同一可执行文件和场景设置的已有帧")
    parser.add_argument("--cpu-only", action="store_true", help="不截帧，交替测量同一角色播放时的 CPU 范围用时")
    parser.add_argument("--analyze", action="store_true", help="从各次已记录的回放结果重新计算对照")
    args = parser.parse_args()
    if args.analyze:
        compare()
        return
    exe = args.exe.resolve(strict=True)
    tools = args.renderdoc.resolve(strict=True)
    version = hashlib.sha256(exe.read_bytes()).hexdigest()
    work = ROOT/"tmp/animated-shadow"
    work.mkdir(parents=True,exist_ok=True)
    OUT.mkdir(parents=True,exist_ok=True)
    if args.cpu_only:
        measure_cpu(exe,version)
        return
    cases = {
        "left-off": (-.9,0,True,"All point lights",False),
        "left-on": (-.9,512,True,"All point lights",False),
        "right-off": (.9,0,True,"All point lights",False),
        "right-on": (.9,512,True,"All point lights",False),
        "lamp-off": (-.9,512,False,"All point lights",False),
        "cluster": (-.9,512,True,"Clustered lights",False),
        "restir": (-.9,512,True,"ReSTIR DI",False),
        "lights-out": (-.9,512,True,"All point lights",False),
        "two-lamps": (-.9,512,True,"All point lights",False),
        "two-lamps-off": (-.9,0,True,"All point lights",False),
        "actor-only": (-.9,512,True,"All point lights",False),
        "actor-only-off": (-.9,0,True,"All point lights",False),
        "moving": (-.9,512,True,"All point lights",True),
        "moving-off": (-.9,0,True,"All point lights",True),
    }
    for label in args.cases or cases:
        path = work/("shadow-"+label+".redflag")
        fixture(path, *cases[label])
        if label=="lights-out" or label.startswith(("two-lamps","actor-only")):
            scene=yaml.safe_load(path.read_text(encoding="utf8"))
            lamp=scene["Entities"][-1]
            if label=="lights-out":
                lamp["DirectionalLightComponent"]["Intensity"]=0.
            elif label.startswith("actor-only"):
                scene["Entities"]=[entity for entity in scene["Entities"] if entity["Entity"]!=103]
            else:
                lamp["DirectionalLightComponent"]["Color"]=[1,0,0]
                second=dict(lamp,Entity=105,DirectionalLightComponent=dict(
                    lamp["DirectionalLightComponent"],Color=[0,0,1],Direction=[-.68,-1,-.45]))
                scene["Entities"].append(second)
            path.write_text(yaml.safe_dump(scene,allow_unicode=True,sort_keys=False),encoding="utf8")
        previous = ROOT/"tmp/material-reference"/f"{path.stem}-{version[:12]}-600-f8"
        entry = previous/"assets/scenes/Sponza-Atrium.redflag"
        frame_path = previous/"frame-path.txt"
        reuse = args.reuse and frame_path.is_file() and Path(frame_path.read_text(encoding="utf8")).is_file()
        if reuse:
            assert entry.read_bytes()==path.read_bytes()
            assert yaml.safe_load(Path(str(entry)+".render.yaml").read_text())==yaml.safe_load(Path(str(path)+".render.yaml").read_text())
        else:
            subprocess.run([sys.executable,str(ROOT/"tests/capture_material_studio.py"),"--exe",str(exe),
                            "--scene",str(path),"--width","600","--renderdoc",str(tools),"--frame","8"],check=True)
        capture = previous.joinpath("frame-path.txt").read_text(encoding="utf8")
        env = dict(os.environ,RF_SHADOW_CAP=capture,RF_SHADOW_DIR=str(previous),RF_SHADOW_TIMING=str(int(label.startswith("moving"))))
        run_renderdoc(tools,ROOT/"tests/replay_animated_shadows.py",ROOT,env)
        report = json.loads((previous/"shadow.json").read_text(encoding="utf8"))
        if "error" in report: raise RuntimeError(report["error"])
        report.update(case=label,exe=version,capture=capture,sha256=hashlib.file_digest(open(capture,"rb"),"sha256").hexdigest())
        (OUT/(label+".json")).write_text(json.dumps(report,indent=2)+"\n",encoding="utf8")
        general = json.loads((previous/"replay.json").read_text(encoding="utf8"))
        w,h = general["size"]
        rgb = np.fromfile(previous/"display.f32",np.float32).reshape(h,w,4)[:,:,:3]
        Image.fromarray(np.rint(np.clip(rgb,0,1)*255).astype(np.uint8)).save(OUT/(label+".png"))
    if not args.cases: compare()


def measure_cpu(exe, version):
    env=dict(os.environ,PATH=str(ROOT/"bin/Debug-windows-x86_64/RedflagForge")+os.pathsep+os.environ["PATH"])
    work=ROOT/"tmp/animated-shadow/cpu"
    prepare(work)
    runs=[]
    for size in [0,512,512,0]:
        scene=work/"assets/scenes/role.redflag"
        fixture(scene,-.9,size,True,"All point lights",True)
        with (work/"run.log").open("w") as log:
            subprocess.run([str(exe),"--scene",str(scene),"--hidden","--frames","90"],env=env,cwd=work,
                           stdout=log,stderr=subprocess.STDOUT,timeout=120,check=True)
        events=json.loads((work/"RedflagProfile-Runtime.json").read_text(encoding="utf8"))["traceEvents"]
        durations=[round(e["dur"]/1000,3) for e in events if e.get("name")=="Rasterize lighting surfaces"][20:]
        assert len(durations)==70
        runs.append(dict(size=size,samples_ms=durations,median_ms=median(durations)))
    result=dict(exe=version,runs=runs,median_ms={str(size):median(v for r in runs if r["size"]==size
                  for v in r["samples_ms"]) for size in [0,512]})
    (OUT/"cpu.json").write_text(json.dumps(result,indent=2)+"\n",encoding="utf8")


def compare():
    def load(name, field, channels):
        version=json.loads((OUT/(name+".json")).read_text())["exe"]
        work = ROOT/"tmp/material-reference"/f"shadow-{name}-{version[:12]}-600-f8"
        w,h = json.loads((work/"replay.json").read_text())["shading_size"]
        return np.fromfile(work/field,np.float32).reshape(h,w,channels)
    summary = {}
    for name in ("left","right"):
        off,on = name+"-off",name+"-on"
        surfaces = load(on,"receivers.f32",12)
        floor = (surfaces[:,:,3] != 0) & (abs(surfaces[:,:,1]) < .003) & (surfaces[:,:,5] > .99)
        occupied = surfaces[:,:,3] != 0
        before, after = load(off,"direct.f32",4)[:,:,:3],load(on,"direct.f32",4)[:,:,:3]
        delta = (before-after).mean(2)
        dark = floor & (delta>.03)
        selfshadow = occupied & ~floor & (delta>.03)
        assert dark.sum()>100 and selfshadow.sum()>5 and np.min(delta)>-.001
        center = surfaces[:,:,0][dark].mean()
        color = load(on,"light.f32",4)[:,:,:3]
        ambient = color-after
        before_ambient = load(off,"light.f32",4)[:,:,:3]-before
        error = abs(ambient-before_ambient)[occupied].max()
        assert error<.001 and np.median(ambient[dark])>.01
        summary[name] = dict(floor_pixels=int(dark.sum()),self_shadow_pixels=int(selfshadow.sum()),
            centroid_world_x=float(center),ambient_difference_max=float(error),ambient_in_shadow=float(np.median(ambient[dark])))
    assert abs(summary["right"]["centroid_world_x"]-summary["left"]["centroid_world_x"]-1.8)<.15
    for a,b in [("left-off","lamp-off"),("left-on","cluster")]:
        error=float(abs(load(a,"direct.f32",4)[:,:,:3]-load(b,"direct.f32",4)[:,:,:3]).max())
        summary[a+"_"+b] = dict(max_direct_difference=error)
        assert error<.002
    # ReSTIR 会沿用邻居和上一帧选的灯。检查完全遮住的地面，同时保留边缘的逐像素误差。
    full=load("left-on","direct.f32",4)[:,:,:3]
    sampled=load("restir","direct.f32",4)[:,:,:3]
    lit=load("left-off","direct.f32",4)[:,:,:3]
    surface=load("left-on","receivers.f32",12)
    ground=(surface[:,:,3]>0)&(abs(surface[:,:,1])<.003)&(surface[:,:,5]>.99)
    fully_shadowed=ground & (full.max(2)<1.e-6) & (lit.mean(2)>.03)
    error=abs(full-sampled)
    assert fully_shadowed.sum()>100 and sampled[fully_shadowed].max()<1e-6 and error.mean()<.001
    summary["restir"]=dict(max_direct_difference=float(error.max()),mean_direct_difference=float(error.mean()),
                           dark_floor_pixels=int(fully_shadowed.sum()))
    out = load("lights-out","direct.f32",4)
    assert abs(out[:,:,:3]).max()==0.
    surfaces = load("two-lamps","receivers.f32",12)
    floor = (surfaces[:,:,3]>0)&(abs(surfaces[:,:,1])<.003)&(surfaces[:,:,5]>.99)
    delta = load("two-lamps-off","direct.f32",4)-load("two-lamps","direct.f32",4)
    red = floor & (delta[:,:,0]>.03) & (delta[:,:,2]<.001)
    blue = floor & (delta[:,:,2]>.03) & (delta[:,:,0]<.001)
    assert red.sum()>100 and blue.sum()>100
    summary["two_lamps"] = dict(only_red_blocked=int(red.sum()),only_blue_blocked=int(blue.sum()))
    solo=load("actor-only","direct.f32",4)[:,:,:3]
    delta=load("actor-only-off","direct.f32",4)[:,:,:3]-solo
    solo_pixels=load("actor-only","receivers.f32",12)[:,:,3]>0
    assert (delta.mean(2)[solo_pixels]>.03).sum()>50 and solo[solo_pixels].max()>.1
    summary["actor_only"] = dict(shadowed_pixels=int((delta.mean(2)[solo_pixels]>.03).sum()))
    (OUT/"comparison.json").write_text(json.dumps(summary,indent=2)+"\n",encoding="utf8")
    print(json.dumps(summary,indent=2))


if __name__=="__main__": main()
