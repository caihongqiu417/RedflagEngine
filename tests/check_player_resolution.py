"""用同一份解压的游戏测两个 3D 分辨率。保留每帧耗时、相机位置与独立窗口的大小。"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import statistics
import subprocess
import tempfile
import time
import zipfile

import win32con
import pywintypes
import win32event
import win32gui
import win32process
import win32service

ROOT = Path(__file__).resolve().parents[1]


def until(check,seconds=90):
    end = time.monotonic()+seconds
    while time.monotonic()<end:
        value = check()
        if value: return value
        time.sleep(.05)
    raise TimeoutError("Game did not reach the requested state")


def game(project,study,argument,prefix):
    session = study/prefix
    session.mkdir()
    exe = project/"RedflagForge.exe"
    env = dict(os.environ,REDFLAG_PLAYTEST=str(session))
    cmd = [str(exe),"--vulkan","--play","assets/scenes/Watt-Station.redflag","--frames","66",*argument]
    name = f"RenderSize{os.getpid()}{prefix}"
    desktop = win32service.CreateDesktop(name,0,win32con.GENERIC_ALL,None)
    info = win32process.STARTUPINFO()
    info.lpDesktop = "WinSta0\\"+name
    proc,thread,pid,_ = win32process.CreateProcess(None,subprocess.list2cmdline(cmd),None,None,False,
                                        win32con.CREATE_NO_WINDOW,env,str(project),info)
    thread.Close()
    try:
        def window():
            if win32process.GetExitCodeProcess(proc)!=259: raise RuntimeError("Early game exit")
            try: windows = desktop.EnumDesktopWindows()
            except pywintypes.error: return None
            return next((w for w in windows if win32process.GetWindowThreadProcessId(w)[1]==pid
                        and win32gui.GetWindowText(w)=="Redflag Game"),None)
        hwnd = until(window)
        _,_,width,height = win32gui.GetClientRect(hwnd)
        assert (width,height)==(1920,1080),(width,height)
        if win32event.WaitForSingleObject(proc,90000)!=win32con.WAIT_OBJECT_0: raise TimeoutError("Frame limit")
        assert win32process.GetExitCodeProcess(proc)==0
    finally:
        if win32process.GetExitCodeProcess(proc)==259: win32process.TerminateProcess(proc,1)
        proc.Close()
        desktop.CloseDesktop()
    frames = [json.loads(row) for row in (session/"frames.jsonl").read_bytes().splitlines()]
    frames = [row for row in frames if 16<=row["seq"]<=60]
    assert len(frames)==45 and not any(r["inputs"] for r in frames)
    # 无输入时跟随相机仍会修正约百分之几毫米。以一毫米精度核对本轮与其他轮的观察位置。
    cameras = {tuple(round(value,3) for value in next(a["position"] for a in r["actors"] if a["id"]=="1100")) for r in frames}
    assert len(cameras)==1,cameras
    samples = [r["frame_ms"] for r in frames]
    return dict(size=argument[-1] if argument else "default",client=[width,height],first=16,last=60,
                camera=cameras.pop(),frame_ms=samples,p50=statistics.median(samples),
                p95=statistics.quantiles(samples,n=100,method="inclusive")[94])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive",type=Path)
    parser.add_argument("--runtime",type=Path,help="先用新构建复测时替换临时解压目录里的程序")
    parser.add_argument("--renderdoc",type=Path,help="只做默认与 720p 的 Vulkan 附件尺寸核验，指定 qrenderdoc.exe 所在目录")
    parser.add_argument("--output",type=Path,default=ROOT/"output/validation/packaged-watt/resolution.json")
    args = parser.parse_args()
    ctypes.windll.user32.SetProcessDPIAware()
    with tempfile.TemporaryDirectory(prefix="redflag-resolution-") as folder:
        project = Path(folder)
        with zipfile.ZipFile(args.archive) as package: package.extractall(project)
        if args.runtime: shutil.copyfile(args.runtime,project/"RedflagForge.exe")
        binary = hashlib.sha256((project/"RedflagForge.exe").read_bytes()).hexdigest()
        if args.renderdoc:
            from capture_material_studio import run_renderdoc
            rd = args.renderdoc.resolve(strict=True)
            for size,internal in (("",[1920,1080]),("1280x720",[1280,720])):
                name = size or "default"
                directory = ROOT/"output/captures/local/watt-resolution"/binary[:12]/name
                directory.mkdir(parents=True,exist_ok=True)
                env = dict(os.environ,RF_STUDIO_GAME="assets/scenes/Watt-Station.redflag",RF_STUDIO_EXE=str(project/"RedflagForge.exe"),
                    RF_STUDIO_TOOLS=str(rd),RF_STUDIO_RENDER_SIZE=size,RF_STUDIO_WORK=str(project),
                    RF_STUDIO_CAPTURE=str(directory/"game.rdc"),RF_STUDIO_FRAME="12")
                run_renderdoc(rd,ROOT/"tests/capture_material_studio.py",ROOT,env)
                capture = Path((project/"frame-path.txt").read_text(encoding="utf8").strip())
                out = args.output.with_name("resolution-"+name+".json")
                env.update(RF_GAME_RESOLUTION_RDC=str(capture),RF_GAME_RESOLUTION_JSON=str(out))
                run_renderdoc(rd,ROOT/"tests/replay_player_resolution.py",ROOT,env)
                evidence = json.loads(out.read_bytes())
                assert evidence["surface"]["attachments"][0]["size"]==internal and evidence["hud"]["attachments"][0]["size"]==[1920,1080],evidence
                evidence["capture"] = capture.relative_to(ROOT).as_posix()
                evidence["exe_sha256"] = binary
                out.write_text(json.dumps(evidence,indent=2)+"\n",encoding="utf8")
            return
        rows = []
        for run,options in enumerate(([],["--render-size","1280x720"],["--render-size","1280x720"],["--render-size","1920x1080"])):
            row = game(project,project,options,str(run+1))
            rows.append(row)
            print(row["size"],row["p50"],row["p95"],flush=True)
        assert len({tuple(row["camera"]) for row in rows})==1
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(dict(exe_sha256=binary,runs=rows),indent=2)+"\n",encoding="utf8")


if __name__=="__main__":
    main()
