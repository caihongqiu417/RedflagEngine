"""在正式 Forge 中打开两份已发布的审查记录，截图并检查它是否显示了对应的原始图片。"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

import cv2
import numpy as np
import pywintypes
import win32con
import win32event
import win32gui
import win32process
import win32service
from PIL import Image

from check_forge_play import wait_for
from profile_native_navigation import ROOT, prepare, save_view


def picture_match(screenshot, reference):
    # 面板随屏幕 DPI 缩放。在面板左半部匹配原图，检测上下颠倒、打开错案例和黑图。
    source=cv2.imread(str(reference),cv2.IMREAD_GRAYSCALE)
    screen=cv2.imread(str(screenshot),cv2.IMREAD_GRAYSCALE)[130:725,63:572]
    width=source.shape[1]
    best=(0.,0)
    for target in range(320,461,2):
        small=cv2.resize(source,None,fx=target/width,fy=target/width,interpolation=cv2.INTER_LINEAR)
        core=small[4*small.shape[0]//10:8*small.shape[0]//10,small.shape[1]//10:9*small.shape[1]//10]
        score=cv2.minMaxLoc(cv2.matchTemplate(screen,core,cv2.TM_CCOEFF_NORMED))[1]
        if score>best[0]: best=(score,target)
    return best


def capture(binary,case,page,project,output,focus=None):
    work=Path(tempfile.mkdtemp(prefix="forge-review-",dir=ROOT/"tmp"))
    prepare(work)
    panel,outer=("1420,1100",(1600,1260)) if page=="playtest" else ("850,665",(1260,865))
    (work/"imgui-vulkan.ini").write_text(f"[Window][创作审查]\nPos=70,65\nSize={panel}\nCollapsed=0\n",encoding="utf8")
    name="CreationReview"+str(os.getpid())
    desktop=win32service.CreateDesktop(name,0,win32con.GENERIC_ALL,None)
    startup=win32process.STARTUPINFO()
    startup.lpDesktop="WinSta0\\"+name
    game=ROOT/"RedflagForge"
    scene=game/"assets/scenes/Character-Walk.redflag"
    env=dict(os.environ,REDFLAG_PROJECT=str(project),REDFLAG_REVIEW=case,REDFLAG_REVIEW_PAGE=page,
             PATH=str(ROOT/"bin/Debug-windows-x86_64/RedflagForge")+os.pathsep+os.environ["PATH"])
    env.update(focus or {})
    command=subprocess.list2cmdline([str(binary),"--vulkan","--scene",str(scene)])
    process,thread,pid,_=win32process.CreateProcess(None,command,None,None,False,win32con.CREATE_NO_WINDOW,
                                                    env,str(work),startup)
    thread.Close()
    try:
        def find():
            try:
                return next((int(w) for w in desktop.EnumDesktopWindows() if win32process.GetWindowThreadProcessId(w)[1]==pid
                             and win32gui.GetWindowText(w)=="Redflag Forge"),None)
            except pywintypes.error: return None
        hwnd=wait_for(find,process)
        win32gui.SetWindowPos(hwnd,None,0,0,*outer,win32con.SWP_NOMOVE|win32con.SWP_NOZORDER|win32con.SWP_NOACTIVATE)
        def rendered():
            save_view(hwnd,output)
            with Image.open(output) as picture:
                return max(np.asarray(picture.crop((70,80,890,730))).std(axis=(0,1)))>12
        wait_for(rendered,process)
        if page=="playtest":
            # 历史较长时两条时间线要分多批读入，指定的帧直到那一批到达才会选中。
            time.sleep(8)
        else: time.sleep(2)
        save_view(hwnd,output)
        win32gui.PostMessage(hwnd,win32con.WM_CLOSE,0,0)
        assert win32event.WaitForSingleObject(process,20000)==win32con.WAIT_OBJECT_0
        assert win32process.GetExitCodeProcess(process)==0
    finally:
        if win32process.GetExitCodeProcess(process)==259: win32process.TerminateProcess(process,1)
        process.Close()
        desktop.CloseDesktop()


if __name__=="__main__":
    args=argparse.ArgumentParser(description=__doc__)
    args.add_argument("--exe",type=Path,default=ROOT/"bin/Debug-windows-x86_64/RedflagForge/RedflagForge.exe")
    args.add_argument("--project",type=Path,default=ROOT/"RedflagForge")
    args.add_argument("--session",help="额外打开一份实际试玩的记录")
    args.add_argument("--change",help="额外打开一次待审资产修改")
    args.add_argument("--frame", type=int)
    args.add_argument("--baseline")
    args.add_argument("--base-frame", type=int)
    args=args.parse_args()
    ctypes.windll.user32.SetProcessDPIAware()
    out=ROOT/"output/validation/creation-review"
    out.mkdir(parents=True,exist_ok=True)
    exe=args.exe.resolve(strict=True)
    # 直接使用本次构建的副本，给仍在迭代的正式程序留下重新链接的机会。
    binary=Path(tempfile.mkdtemp(prefix="review-bin-",dir=ROOT/"tmp"))/"RedflagForge.exe"
    shutil.copyfile(exe,binary)
    results={"exe_sha256":hashlib.sha256(binary.read_bytes()).hexdigest(),"images":{}}
    for case,file in (("animated-shadows","left-off.png"),("animation-events","forge-pose.png")):
        shot=out/(case+".png")
        capture(binary,case,"images",ROOT/"RedflagForge",shot)
        score,width=picture_match(shot,ROOT/"output/validation"/case/file)
        results["images"][case]={"screenshot":shot.name,"reference":file,"correlation":score,"display_width":width}
        assert score>.74,(case,score,width)
    if args.session:
        focus={"REDFLAG_REVIEW_FRAME":str(args.frame or ""),"REDFLAG_REVIEW_BASELINE":args.baseline or "",
               "REDFLAG_REVIEW_BASE_FRAME":str(args.base_frame or "")}
        capture(binary,args.session,"playtest",args.project.resolve(),out/"forge-playtest.png",focus)
        results["playtest"]={"session":args.session,"frame":args.frame,"baseline":args.baseline,"base_frame":args.base_frame}
    if args.change: capture(binary,args.change,"change",args.project.resolve(),out/"forge-asset-change.png")
    (out/"forge.json").write_text(json.dumps(results,indent=2)+"\n",encoding="utf8")
    print(results)
