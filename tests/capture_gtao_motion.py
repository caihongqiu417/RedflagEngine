"""在 Forge 主相机下检查 GTAO 的输入、间接光合成和显示图。运行目录为仓库根。"""
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
from check_gtao_motion import floor_samples, visibility

ROOT = Path(__file__).resolve().parents[1]
WIDTH = 480


def write_scene(path, frame):
    studio = yaml.safe_load((ROOT/"RedflagForge/assets/scenes/Camera-Studio.redflag").read_text(encoding="utf8"))
    camera = next(e for e in studio["Entities"] if "CameraComponent" in e)
    eye = [-.9+.15*frame,1.65,3.8]
    dx,dy,dz = .15-eye[0], .25-eye[1], -eye[2]
    camera["TransformComponent"].update(Translation=eye,Rotation=[math.atan2(dy,math.hypot(dx,dz)),math.atan2(-dx,-dz),0])
    camera["CameraComponent"]["Camera"]["PerspectiveFov"] = math.radians(44)
    entities = [camera]
    for i,(name,pos,size) in enumerate([("地面",[0,-.05,0],[12,.1,12]),("箱子",[0,.5,0],[1,1,1]),
                                        ("立柱",[1.05,.5,0],[.12,1,.12])]):
        entities.append(dict(Entity=500+i,TagComponent=dict(Tag=name),
            TransformComponent=dict(Translation=pos,Rotation=[0,0,0],Scale=size),
            MeshRendererComponent=dict(Color=[1,1,1,1],BaseColor=[.55,.55,.55,1],Metallic=0,Roughness=1)))
    scene = dict(Scene="接触遮蔽",Environment=studio["Environment"],Entities=entities)
    path.write_text(yaml.safe_dump(scene,allow_unicode=True,sort_keys=False),encoding="utf8")
    settings = dict(Shading="Lit",Indirect="Diffuse IBL",GTAO=True,AORadius=.65,AOIntensity=1,
                    TAA=True,HistoryWeight=0,SSR=False,SpecularIBL=False,PreviewWidth=WIDTH)
    Path(str(path)+".render.yaml").write_text(yaml.safe_dump(settings),encoding="utf8")


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe",type=Path,default=ROOT/"bin/Debug-windows-x86_64/RedflagForge/RedflagForge.exe")
    parser.add_argument("--output",type=Path,default=ROOT/"output/validation/gtao-motion/forge")
    parser.add_argument("--views",type=int,nargs="*",default=[0,6,12])
    parser.add_argument("--reuse",action="store_true",help="复用同一可执行文件、同一场景捕获的已回放帧")
    parser.add_argument("--time",action="store_true",help="对已有捕获预热一次，再读取 11 次 GPU 事件时间")
    opts=parser.parse_args()
    exe = opts.exe.resolve(strict=True)
    with exe.open("rb") as stream:
        build = hashlib.file_digest(stream,"sha256").hexdigest()
    folder=ROOT/"tmp/gtao-forge"
    folder.mkdir(parents=True,exist_ok=True)
    opts.output.mkdir(parents=True,exist_ok=True)
    for n in opts.views:
        path=folder/f"AO-Contact-{n}.redflag"
        write_scene(path,n)
        work=ROOT/"tmp/material-reference"/f"{path.stem}-{build[:12]}-{WIDTH}-f8"
        if opts.reuse and (work/"ao-output.f32").is_file():
            assert work.joinpath("assets/scenes/Sponza-Atrium.redflag").read_bytes()==path.read_bytes()
            settings=Path(str(path)+".render.yaml").read_text(encoding="utf8")
            assert yaml.safe_load(settings)==yaml.safe_load((work/"assets/scenes/Sponza-Atrium.redflag.render.yaml").read_text(encoding="utf8"))
        else:
            env=dict(os.environ,RF_EXPORT_GTAO="1",RF_STUDIO_GAME="assets/scenes/Sponza-Atrium.redflag")
            subprocess.run([sys.executable,ROOT/"tests/capture_material_studio.py","--exe",exe,
                            "--renderdoc",ROOT/"tmp/tools/renderdoc/RenderDoc_1.47_64","--scene",path,
                            "--width",str(WIDTH),"--frame","8"],env=env,check=True)
        rdc=Path((work/"frame-path.txt").read_text(encoding="utf8"))
        if opts.time:
            from capture_material_studio import run_renderdoc
            env=dict(os.environ,RF_EXPORT_GTAO="1",RF_TIME_GTAO="1",RF_STUDIO_CAPTURE=str(rdc),RF_STUDIO_EXPORT=str(work))
            run_renderdoc(ROOT/"tmp/tools/renderdoc/RenderDoc_1.47_64",ROOT/"tests/replay_material_studio.py",ROOT,env)
        info=json.loads((work/"replay.json").read_text(encoding="utf8"))
        w,h=info["size"]
        aw,ah=info["shading_size"]
        color=np.fromfile(work/"display.f32",np.float32).reshape(h,w,4)[:,:,:3]
        Image.fromarray(np.rint(color.clip(0,1)*255).astype("uint8")).save(opts.output/f"lit-{n}.png")
        ao=np.fromfile(work/"ao-output.f32",np.float32).reshape(ah,aw,4)[:,:,3]
        surfaces=np.fromfile(work/"ao-surfaces.f32",np.float32).reshape(ah,aw,12)
        visual=np.repeat(np.rint(ao.clip(0,1)*255).astype("uint8")[:,:,None],3,axis=2)
        visual[surfaces[:,:,3]==0]=[137,151,165]
        image=Image.fromarray(visual)
        image.save(opts.output/f"ao-{n}.png")
        if n==6:
            image.crop((aw//2,ah*45//100,aw*76//100,ah*68//100)).save(opts.output/"post-detail.png")
        foot=(abs(surfaces[:,:,1])<.00002)&(abs(surfaces[:,:,0])<.41)&(surfaces[:,:,2]>.52)&(surfaces[:,:,2]<.60)
        if np.count_nonzero(foot)<30 or np.mean(ao[foot])>.9: raise ValueError("未从场景光照中读到箱脚遮蔽")
        # 沿柱子周围四条同心圆读地面。用射线参考扣掉方柱本身随方位变化的阴影，剩下的变化能量出灰带。
        angles=np.arange(192)*(2*np.pi/192)
        radii=np.array([.12,.18,.26,.34])
        xx,zz=1.05+radii[:,None]*np.cos(angles),radii[:,None]*np.sin(angles)
        points=np.column_stack((xx.ravel(),np.zeros(xx.size),zz.ravel(),np.ones(xx.size)))
        matrices=np.fromfile(work/"ao-camera.bin",np.float32)[:32].reshape(2,4,4).transpose(0,2,1)
        pixels=np.empty((ah,aw,4),np.float32)
        pixels[:,:,:3]=surfaces[:,:,:3]
        pixels[:,:,1][surfaces[:,:,3]==0]=-100
        pixels[:,:,3]=ao
        samples=floor_samples(pixels,matrices[0]@matrices[1],points,3)
        diff=(samples-visibility(points)).reshape(xx.shape)
        bands=np.abs(diff-np.roll(diff,1,axis=1))
        with rdc.open("rb") as stream:
            digest=hashlib.file_digest(stream,"sha256").hexdigest()
        info.update(exe_sha256=build,rdc=rdc.relative_to(ROOT).as_posix(),rdc_sha256=digest,box_foot_visibility=float(ao[foot].mean()),
                    box_foot_pixels=int(foot.sum()),post_ring_visible_points=int(np.isfinite(diff).sum()),
                    post_ring_mae=float(np.nanmean(abs(diff))),post_ring_step_error=float(np.nanmean(bands)))
        (opts.output/f"frame-{n}.json").write_text(json.dumps(info,indent=2)+"\n",encoding="utf8")


if __name__=="__main__":
    main()
