"""在正式游戏窗口中点击和拖动，检查队友通讯与 C# 游戏脚本分别收到哪些输入。"""
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

import pywintypes
import win32api
import win32con
import win32event
import win32gui
import win32process
import win32service
import yaml
from PIL import Image

from check_forge_play import wait_for
from profile_native_navigation import ROOT, prepare, save_view
import sys
sys.path.insert(0,str(ROOT/"scripts/redflag_mcp"))
from playtest import write_json, read_json, await_receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=ROOT / "bin/Debug-windows-x86_64/RedflagForge/RedflagForge.exe")
    parser.add_argument("--record-old", action="store_true", help="使用保留的旧构建复现通讯窗口的点击串进游戏")
    args = parser.parse_args()
    exe = args.exe.resolve(strict=True)
    run = Path(tempfile.mkdtemp(prefix="hud-input-",dir=ROOT/"tmp"))
    scripts = run/"Resources/Scripts"
    scripts.mkdir(parents=True)
    prepare(run)
    core = scripts/"Redflag-ScriptCore.dll"
    shutil.copy2(ROOT/"RedflagForge/Resources/Scripts/Redflag-ScriptCore.dll", core)
    compiler = Path(os.environ["WINDIR"])/"Microsoft.NET/Framework64/v4.0.30319/csc.exe"
    subprocess.run([str(compiler),"/nologo","/target:library","/out:"+str(scripts/"Sandbox.dll"),
                    "/reference:"+str(core),str(ROOT/"tests/GameMouseProbe.cs")],check=True)
    source = ROOT/"RedflagForge/assets/scenes/Character-Walk.redflag"
    scene = yaml.safe_load(source.read_text("utf8"))
    scene["Entities"][0]["ScriptComponent"] = dict(Class="RoutingTest.Probe",ScriptFields=[])
    entry = run/"assets/scenes/Mouse.redflag"
    entry.write_text(yaml.safe_dump(scene,sort_keys=False,allow_unicode=True),encoding="utf8")
    shutil.copy2(str(source)+".render.yaml",str(entry)+".render.yaml")
    (run/"imgui-vulkan.ini").write_text("[Window][队友通讯]\nPos=80,120\nSize=570,470\nCollapsed=0\n",encoding="utf8")

    ctypes.windll.user32.SetProcessDPIAware()
    name = "HudMouse"+str(os.getpid())
    desktop = win32service.CreateDesktop(name,0,win32con.GENERIC_ALL,None)
    startup = win32process.STARTUPINFO()
    startup.lpDesktop = "WinSta0\\"+name
    command = subprocess.list2cmdline([str(exe),"--play",str(entry)])
    env = dict(os.environ,PATH=str(ROOT/"bin/Debug-windows-x86_64/RedflagForge")+os.pathsep+os.environ["PATH"])
    session = run/"captures/playtests/mouse"
    if not args.record_old:
        (session/"commands").mkdir(parents=True)
        (session/"receipts").mkdir()
        write_json(session/"session.json",{"scene":str(entry),"intent":"检查鼠标、按键与操作权移交"})
        write_json(session/"handoff.json",{"owner":"agent","generation":1})
        env["REDFLAG_PLAYTEST"]=str(session)
    proc,thread,pid,_ = win32process.CreateProcess(None,command,None,None,False,win32con.CREATE_NO_WINDOW,env,str(run),startup)
    thread.Close()
    out = ROOT/"output/validation/watt-station/hud/input"/("before" if args.record_old else "after")
    out.mkdir(parents=True,exist_ok=True)
    try:
        def window():
            try:
                return next((int(w) for w in desktop.EnumDesktopWindows() if win32process.GetWindowThreadProcessId(w)[1]==pid
                             and win32gui.GetWindowText(w)=="Redflag Game"),None)
            except pywintypes.error: return None
        wnd = wait_for(window,proc)
        state = run/"mouse.txt"
        wait_for(state.is_file,proc)
        win32gui.PostMessage(wnd,win32con.WM_SETFOCUS,0,0)
        number = 0
        generation = 1
        receipts = []
        def action(op,**fields):
            nonlocal number
            number += 1
            cmd=dict(id=number,generation=generation,op=op)
            cmd.update(fields)
            write_json(session/"commands"/f"{number:06}.json",cmd)
            ack = await_receipt(session,number)
            receipts.append(ack)
            return ack

        def mouse(point,message,held=False,settle=.22):
            if args.record_old:
                win32gui.PostMessage(wnd,message,win32con.MK_LBUTTON if held else 0,win32api.MAKELONG(*point))
            else:
                op={win32con.WM_MOUSEMOVE:"move",win32con.WM_LBUTTONDOWN:"hold",win32con.WM_LBUTTONUP:"release"}[message]
                assert action(op,key="MouseLeft",x=point[0],y=point[1])["ok"]
            if settle: time.sleep(settle)
        def move(point,held=False,settle=.22): mouse(point,win32con.WM_MOUSEMOVE,held,settle)
        def down(point): mouse(point,win32con.WM_LBUTTONDOWN,True)
        def up(point): mouse(point,win32con.WM_LBUTTONUP)

        def toggle_communications():
            if not args.record_old:
                assert action("tap",key="T")["ok"]
                time.sleep(.35)
                return
            flags = 1|(win32api.MapVirtualKey(ord("T"),0)<<16)
            win32gui.PostMessage(wnd,win32con.WM_KEYDOWN,ord("T"),flags)
            time.sleep(.20)
            win32gui.PostMessage(wnd,win32con.WM_KEYUP,ord("T"),flags|(3<<30))
            time.sleep(.35)

        def picture(name, require_scene=False):
            path = run/(name+".png")
            save_view(wnd,path)
            with Image.open(path) as picture:
                if require_scene and all(lo == hi for lo,hi in picture.crop((250,190,1600,900)).getextrema()):
                    return None
                picture.crop((70,110,660,265)).save(out/(name+".png"))
                return picture.getpixel((100,150))

        recorded = {}
        def mark(label): recorded[label] = state.read_text().splitlines()[1:]
        game, panel, close = (900,650),(110,180),(635,131)
        time.sleep(.35)
        empty = wait_for(lambda:picture("game",True),proc)
        move(game); down(game); up(game)
        mark("click_game")
        toggle_communications()
        assert picture("communications") != empty
        move(close); down(close); up(close)
        assert picture("after-close") == empty, "title-bar click did not close communications"
        mark("close_communications")
        toggle_communications()

        # 两次拖动跨过相同的分界线，只有按在游戏里开始的一次归游戏脚本。
        move(game); down(game); move(panel,True)
        mark("drag_from_game")
        up(panel)
        mark("release_on_panel")
        if not args.record_old: assert picture("before-panel-drag") != empty
        move(panel,settle=0); down(panel); move(game,True)
        mark("drag_from_panel")
        up(game)
        toggle_communications()
        move(game); down(game); up(game)
        mark("click_game_again")

        click = ["down","up"]
        expected = {"click_game":click,"close_communications":click,"drag_from_game":click+["down"],
                    "release_on_panel":click*2,"drag_from_panel":click*2,"click_game_again":click*3}
        if args.record_old:
            assert len(recorded["close_communications"]) == len(recorded["click_game"])+2, recorded
        else:
            assert recorded == expected, recorded
        note = dict(exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),game_script=recorded,
                    communication_opened_and_closed=True)
        if not args.record_old:
            assert action("hold",key="W",lease_ms=200)["ok"]
            time.sleep(.45)
            def frames():
                return [json.loads(line) for line in (session/"frames.jsonl").read_bytes().split(b"\n")[:-1]]
            rows=frames()
            assert any(e["kind"]=="lease_expired" and e["text"]=="W" for r in rows for e in r["events"])
            assert action("hold",key="W")["ok"]
            time.sleep(.12)
            transfer=action("handoff",owner="human")
            assert transfer["ok"] and transfer["generation"]==2
            stale=action("hold",key="W")
            assert not stale["ok"] and stale["reason"]=="stale_generation"
            generation=2
            assert action("tap",key="W")["reason"]=="human_controls_game"
            flags=1|(win32api.MapVirtualKey(ord("W"),0)<<16)
            win32gui.PostMessage(wnd,win32con.WM_KEYDOWN,ord("W"),flags)
            time.sleep(.25)
            win32gui.PostMessage(wnd,win32con.WM_KEYUP,ord("W"),flags|(3<<30))
            time.sleep(.1)
            assert action("end")["reason"]=="human_controls_game"
            assert action("handoff",owner="agent")["generation"]==3
            generation=3
            rows=frames()
            walking=[(r["seq"],e["text"]) for r in rows for e in r["events"] if e["kind"]=="walking"]
            assert [value for _,value in walking]==["begin","end"]*3,walking
            assert any(r["owner"]=="human" and i["key"]=="W" for r in rows for i in r["inputs"])
            actor=lambda r:next(a for a in r["actors"] if a["id"]=="101")
            assert actor(rows[-1])["position"][0] > actor(rows[0])["position"][0]
            note.update(receipts=receipts,walking=walking,actor_first=actor(rows[0]),actor_last=actor(rows[-1]),
                        timed_release=True, stale_rejected=True,human_input=True)
        if args.record_old: win32gui.PostMessage(wnd,win32con.WM_CLOSE,0,0)
        else: assert action("end")["ok"]
        assert win32event.WaitForSingleObject(proc,20000)==win32con.WAIT_OBJECT_0
        if not args.record_old:
            note["status"]=read_json(session/"status.json")
            assert note["status"]["state"]=="stopped"
        (out/"mouse.json").write_text(json.dumps(note,indent=2)+"\n",encoding="utf8")
        print(json.dumps(note))
    finally:
        if win32process.GetExitCodeProcess(proc)==259:win32process.TerminateProcess(proc,1)
        proc.Close()
        desktop.CloseDesktop()


if __name__=="__main__":
    main()
