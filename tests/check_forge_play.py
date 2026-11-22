"""从根目录运行 python tests/check_forge_play.py。用正式 Vulkan 编辑器验证两轮播放与退出中的脚本、角色与场景还原。"""
import copy
import csv
import ctypes
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

import win32api
import win32con
import win32event
import win32gui
import win32process
import win32service
import pywintypes
import yaml

from profile_native_navigation import ROOT, prepare, save_view


def wait_for(test, process, seconds=45):
    deadline = time.monotonic()+seconds
    while time.monotonic()<deadline:
        result = test()
        if result: return result
        if win32process.GetExitCodeProcess(process) != 259: break
        time.sleep(.08)
    raise RuntimeError("Forge did not reach " + test.__name__)


def main():
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path,default=ROOT/"bin/Debug-windows-x86_64/RedflagForge/RedflagForge.exe")
    parser.add_argument("--playtest", action="store_true",help="两轮 F6 写入同一份试玩记录，核对帧号和接管代数")
    options=parser.parse_args()
    exe = options.exe.resolve(strict=True)
    ctypes.windll.user32.SetProcessDPIAware()
    work = Path(tempfile.mkdtemp(prefix="game-play-",dir=ROOT/"tmp"))
    scripts = work/"Resources/Scripts"
    scripts.mkdir(parents=True)  # 不改正式游戏的程序集。
    prepare(work)
    core = scripts/"Redflag-ScriptCore.dll"
    shutil.copy2(ROOT/"RedflagForge/Resources/Scripts/Redflag-ScriptCore.dll",core)
    compiler = Path(os.environ["WINDIR"])/"Microsoft.NET/Framework64/v4.0.30319/csc.exe"
    subprocess.run([str(compiler),"/nologo","/target:library","/out:"+str(scripts/"Sandbox.dll"),
                    "/reference:"+str(core),str(ROOT/"tests/PlayProbe.cs")],check=True)
    source = ROOT/"RedflagForge/assets/scenes/Character-Walk.redflag"
    scene = yaml.safe_load(source.read_text(encoding="utf8"))
    actor = next(e for e in scene["Entities"] if e["Entity"]==102)
    partner = copy.deepcopy(actor)
    partner["Entity"] = 105
    partner["TagComponent"]["Tag"] = "同伴"
    partner["TransformComponent"]["Translation"] = [-.6,0,-.3]
    scene["Entities"].append(partner)
    for e,other,direction in [(actor,105,[1,0,0]),(partner,102,[0,0,1])]:
        e["ScriptComponent"] = dict(Class="PlayTest.Walker",ScriptFields=[
            dict(Name="Partner",Type="Entity",Value=other),dict(Name="Direction",Type="Vector3",Value=direction)])
    sample = work/"assets/scenes/Play.redflag"
    sample.write_text(yaml.safe_dump(scene,allow_unicode=True,sort_keys=False),encoding="utf8")
    shutil.copy2(str(source)+".render.yaml",str(sample)+".render.yaml")
    original = sample.read_bytes()
    out = ROOT/"output/validation/forge-play"
    out.mkdir(parents=True,exist_ok=True)
    name = "RedflagPlay"+str(os.getpid())
    desktop = win32service.CreateDesktop(name,0,win32con.GENERIC_ALL,None)
    info = win32process.STARTUPINFO()
    info.lpDesktop = "WinSta0\\"+name
    env = dict(os.environ,RF_VULKAN_VALIDATION="1")
    session=work/"captures/playtests/f6"
    if options.playtest:
        session.mkdir(parents=True)
        (session/"session.json").write_text(json.dumps({"scene":str(sample),"intent":"双轮 F6 播放"}),encoding="utf8")
        env["REDFLAG_PLAYTEST"]=str(session)
    args = subprocess.list2cmdline([str(exe),"--vulkan","--scene",str(sample)])
    process,thread,pid,_ = win32process.CreateProcess(None,args,None,None,False,win32con.CREATE_NO_WINDOW,env,str(work),info)
    thread.Close()
    saved = work/"play.csv"
    def rows():
        return list(csv.reader(saved.read_text().splitlines())) if saved.exists() else []
    try:
        def window():
            try:
                return next((int(w) for w in desktop.EnumDesktopWindows() if win32process.GetWindowThreadProcessId(w)[1]==pid
                             and win32gui.GetWindowText(w)=="Redflag Forge"),None)
            except pywintypes.error:
                return None
        hwnd = wait_for(window,process)
        def layout_ready():
            p = work/"imgui-vulkan.ini"
            return p.exists() and "[Window][场景视口]" in p.read_text(encoding="utf8")
        wait_for(layout_ready,process)
        win32gui.PostMessage(hwnd,win32con.WM_SETFOCUS,0,0)
        def hotkey(key):
            flags = 1 | (win32api.MapVirtualKey(key,0)<<16)
            win32gui.PostMessage(hwnd,win32con.WM_KEYDOWN,key,flags)
            time.sleep(.14)
            win32gui.PostMessage(hwnd,win32con.WM_KEYUP,key,flags | (3<<30))
            time.sleep(.3)
        # F5 继续只模拟物理。F6 进入主相机的游戏副本；第二轮由关闭窗口触发 OnDestroy。
        hotkey(win32con.VK_F5); hotkey(win32con.VK_F5)
        assert not rows()
        for cycle in range(2):
            hotkey(win32con.VK_F6)
            wait_for(lambda:sum(row[1]=="update" for row in rows())==2*(cycle+1),process)
            if cycle==0:
                save_view(hwnd,out/"playing.png")
                hotkey(win32con.VK_F6)
                wait_for(lambda:sum(row[1]=="destroy" for row in rows())==2,process)
                save_view(hwnd,out/"stopped.png")
            else:
                win32gui.PostMessage(hwnd,win32con.WM_CLOSE,0,0)
        assert win32event.WaitForSingleObject(process,20000)==win32con.WAIT_OBJECT_0 and win32process.GetExitCodeProcess(process)==0
    finally:
        if win32process.GetExitCodeProcess(process)==259: win32process.TerminateProcess(process,1)
        process.Close()
        desktop.CloseDesktop()
    assert sample.read_bytes()==original
    for actor_id,(dx,dz,yaw) in {"102":(1,0,math.pi/2),"105":(0,1,0)}.items():
        events=[row for row in rows() if row[0]==actor_id]
        assert [row[1] for row in events]==["create","update","destroy"]*2 and all(row[2]=="True" for row in events), events
        assert events[0][3:]==events[3][3:]
        for start,end in [(events[0],events[2]),(events[3],events[5])]:
            x,z = float(end[3])-float(start[3]),float(end[4])-float(start[4])
            assert x*dx+z*dz>0 and abs(x*dz-z*dx)<1e-5 and abs(float(end[5])-yaw)<1e-5
    shutil.copy2(saved,out/"lifecycle.csv")
    report = dict(exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),scene_saved_unchanged=True,
                  play_cycles=2,stop_via=["F6","window close"],independent_movement=True,physics_simulation_ran_scripts=False)
    if options.playtest:
        trace=[json.loads(line) for line in (session/"frames.jsonl").read_bytes().splitlines()]
        starts=[row for row in trace if any(e["kind"]=="play" and e["text"]=="start" for e in row["events"])]
        assert [row["seq"] for row in trace]==list(range(1,len(trace)+1))
        assert [row["generation"] for row in starts]==[1,2]
        stopped=json.loads((session/"status.json").read_text("utf8"))
        assert stopped["state"]=="stopped" and stopped["seq"]==len(trace)
        report["playtest"]=dict(frames=len(trace),starts=[row["seq"] for row in starts],status=stopped)
    (out/"report.json").write_text(json.dumps(report,indent=2)+"\n",encoding="utf8")
    print(json.dumps(report))


if __name__=="__main__":
    main()
