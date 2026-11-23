"""在正式 Vulkan Play 窗口抓台阶、越沟和攀爬时的角色姿势。"""
import argparse
import ctypes
import csv
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

import win32con
import win32event
import win32gui
import win32process
import win32service
import pywintypes
import yaml
import numpy as np
from PIL import Image

from check_forge_play import wait_for
from profile_native_navigation import prepare, ROOT, save_view


def setup(work):
    scripts = work / "Resources/Scripts"
    scripts.mkdir(parents=True)
    prepare(work)
    core = scripts / "Redflag-ScriptCore.dll"
    shutil.copy2(ROOT / "RedflagForge/Resources/Scripts" / core.name, core)
    finder = Path(os.environ["ProgramFiles(x86)"]) / "Microsoft Visual Studio/Installer/vswhere.exe"
    vs = Path(subprocess.check_output([str(finder), "-latest", "-property", "installationPath"], text=True).strip())
    subprocess.run([str(vs / "MSBuild/Current/Bin/Roslyn/csc.exe"), "/nologo", "/target:library",
                    "/out:" + str(scripts / "Sandbox.dll"), "/reference:" + str(core),
                    str(ROOT / "tests/fixtures/terrain/TraversalProbe.cs")], check=True)


def transform(position, scale=(1,1,1), rotation=(0,0,0)):
    return dict(Translation=list(position), Scale=list(scale), Rotation=list(rotation))


def platform(uid, z, height, color):
    return dict(Entity=uid, TagComponent=dict(Tag="平台"), TransformComponent=transform((0,(height-.25)/2,z),(3,height+.25,3)),
                RigidBody3DComponent=dict(BodyType="Static", Density=1), BoxCollider3DComponent=dict(HalfSize=[.5,.5,.5]),
                MeshRendererComponent=dict(Color=[1,1,1,1],BaseColor=color,Roughness=.78))


def capture(exe, work, output, name, expected, rise):
    source = ROOT / "RedflagForge/assets/scenes/Camera-Studio.redflag"
    scene = yaml.safe_load(source.read_text(encoding="utf8"))
    camera, actor, _, _, lamp, _ = scene["Entities"]
    camera["TransformComponent"] = transform((5.6,3.1,3.9), rotation=(-.18,.96,0))
    camera["CameraComponent"]["Camera"]["PerspectiveFov"] = .86
    actor.update(TransformComponent=transform((0,.07,-.76)), ModelRendererComponent=dict(
        FilePath="assets/game/station/crew/Operator.glb", Color=[1,1,1,1]),
        ModelAnimationComponent=dict(Clip="walking_nogun-cycle", Playing=True),
        ScriptComponent=dict(Class="TerrainTest.Walker", ScriptFields=[dict(Name="Expected",Type="Int",Value=expected)]))
    lamp["TransformComponent"] = transform((1.8,4,1.3))
    lamp["PointLightComponent"].update(Intensity=22, Radius=12)
    first = platform(11,-1.5,0,[.27,.28,.25,1])
    other = platform(12,2.45 if name=="gap" else 1.5,rise,[.45,.39,.26,1])
    scene.update(Scene="通行测试", SkyVisible=False, Entities=[camera,actor,first,other,lamp])
    if name=="slope":
        # 16 位单通道高度图。每走 0.5 米升高 0.2 米，经过中心的坡顶后以相同坡度下降。
        rows=np.array([0,0,0,16384,32768,49151,65535,49151,32768,16384,0,0],dtype=np.uint16)
        terrain=work/"assets/scenes/test-ramp.png"
        Image.fromarray(np.repeat(rows[:,None],5,axis=1)).save(terrain)
        ground=dict(Entity=13,TagComponent=dict(Tag="折返坡面"),TransformComponent=transform((-1,0,-1.75)),
                    TerrainComponent=dict(Heightmap="assets/scenes/test-ramp.png",HeightScale=.8,CellSize=.5,
                                          Collision=True,ChunkCells=4,Tint=[.48,.42,.28,1]))
        scene["Entities"]=[camera,actor,ground,lamp]
    sample=work/"assets/scenes/Traversal.redflag"
    sample.write_text(yaml.safe_dump(scene,allow_unicode=True,sort_keys=False),encoding="utf8")
    Path(str(sample)+".render.yaml").write_text("Shading: Lit\nSamples: 1\nIndirect: Off\nTAA: true\n",encoding="utf8")
    received=work/"capture.txt"
    received.unlink(missing_ok=True)
    desktop=win32service.CreateDesktop("RedflagTraversal"+name+str(os.getpid()),0,win32con.GENERIC_ALL,None)
    startup=win32process.STARTUPINFO()
    startup.lpDesktop="WinSta0\\RedflagTraversal"+name+str(os.getpid())
    command=subprocess.list2cmdline([str(exe),"--play",str(sample)])
    process,thread,pid,_=win32process.CreateProcess(None,command,None,None,False,win32con.CREATE_NO_WINDOW,
        dict(os.environ,RF_VULKAN_VALIDATION="1"),str(work),startup)
    thread.Close()
    try:
        def window():
            try:
                return next((int(w) for w in desktop.EnumDesktopWindows() if win32process.GetWindowThreadProcessId(w)[1]==pid
                             and win32gui.GetWindowText(w)=="Redflag Game"),None)
            except pywintypes.error:
                return None
        hwnd=wait_for(window,process)
        readout=wait_for(lambda: received.read_text(encoding="utf-8-sig").strip() if received.exists() else None,process,90)
        # 场景进入取样姿势后，脚本停止移动。再给引擎几帧完成蒙皮和显示。
        time.sleep(.8)
        save_view(hwnd,output/(name+".png"))
        win32gui.PostMessage(hwnd,win32con.WM_CLOSE,0,0)
        print(name,readout,flush=True)
        if name!="slope": return readout
        with (work/"slope.csv").open(encoding="utf-8-sig") as trace:
            route=[dict(mode=mode,z_m=float(z),y_m=float(y)) for mode,z,y in csv.reader(trace)]
        assert all(r["mode"]=="Slope" for r in route), route
        heights=[r["y_m"] for r in route]
        assert max(heights)-heights[0]>.75 and max(heights)-heights[-1]>.48
        error=max(abs(r["y_m"]-(.07+.8-.4*abs(r["z_m"]-1.25))) for r in route)
        assert error<.002, error
        return dict(pose=readout,height_error_m=error,route=route)
    finally:
        if win32event.WaitForSingleObject(process,3000)!=win32con.WAIT_OBJECT_0:
            win32process.TerminateProcess(process,1)
        process.Close()
        desktop.CloseDesktop()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe",type=Path,default=ROOT/"bin/Debug-windows-x86_64/RedflagForge/RedflagForge.exe")
    parser.add_argument("--output",type=Path,default=ROOT/"output/validation/terrain-traversal")
    args=parser.parse_args()
    exe=args.exe.resolve(strict=True)
    ctypes.windll.user32.SetProcessDPIAware()
    work=Path(tempfile.mkdtemp(prefix="traversal-play-",dir=ROOT/"tmp"))
    setup(work)
    output=args.output.resolve()
    output.mkdir(parents=True,exist_ok=True)
    report=dict(executable_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),backend="Vulkan Play",samples={})
    for name,mode,height in (("step",2,.2),("gap",3,0),("climb",4,1.42),("slope",1,0)):
        report["samples"][name]=capture(exe,work,output,name,mode,height)
    (output/"play.json").write_text(json.dumps(report,indent=2)+"\n",encoding="utf8")


if __name__=="__main__":
    main()
