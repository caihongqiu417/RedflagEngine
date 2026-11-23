"""从仓库根目录运行 python tests/check_watt_station.py。按真实窗口的按键游玩一局，再重开验证倒地。"""
import argparse
import csv
import ctypes
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
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

from check_forge_play import wait_for
from profile_native_navigation import ROOT, prepare, save_view


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe",type=Path,default=ROOT/"bin/Debug-windows-x86_64/RedflagForge/RedflagForge.exe")
    parser.add_argument("--preview",action="store_true",help="只截正式游戏的开场画面")
    parser.add_argument("--npc-motion",action="store_true",help="通过游戏脚本要求队友取药和递药，记录第三人称窗口")
    parser.add_argument("--latency",choices=("reserve","walk","dispatch"),help="在正式游戏内记录请求入队、保留库存或位置改变的耗时")
    parser.add_argument("--recorded-speech",type=Path,help="把本局的队友回执和中断证据另存在这个目录")
    parser.add_argument("--trials",type=int,default=20)
    parser.add_argument("--landing", action="store_true", help="在正式关卡按一次空格，记录落地时间区间")
    parser.add_argument("--melee", choices=("hit","miss","wall","cancel","movement"), help="验证维修站玩家的移动、镜头和近战")
    parser.add_argument("--interaction", choices=("pickup","repair","revive","equipment"), help="实际按 E、H 与装备键，检查动作和交互结果")
    parser.add_argument("--sentinel", action="store_true", help="核对巡检机追赶玩家时的实体、根骨和脚步")
    parser.add_argument("--watt-terrain",choices=("gap","climb"),help="在测试平台观察瓦特自主跟随领队跨沟、攀台和恢复移动")
    parser.add_argument("--animation-actors", action="store_true", help="在双角色测试场景检查 C# 通知与动作打断")
    parser.add_argument("--animator", type=Path, help="把另存的动作表用于本次落地测试")
    parser.add_argument("--orbit-yaw",type=float,help="在运行副本中指定第三人称环视起始角度（度）")
    parser.add_argument("--start-xz", nargs=2, type=float, metavar=("X", "Z"), help="把运行副本中的玩家移到指定地点，复查同一套 HUD")
    parser.add_argument("--show-help", action="store_true", help="截图时实际按住 F1")
    parser.add_argument("--name",default="third-person-pbr",help="开场截图的文件名，不含扩展名")
    args = parser.parse_args()
    exe = args.exe.resolve(strict=True)
    work = Path(tempfile.mkdtemp(prefix="station-",dir=ROOT/"tmp"))
    scripts = work/"Resources/Scripts"
    scripts.mkdir(parents=True)
    if args.animator:
        # 在本次运行目录内替换动作表，保留正式关卡的资产。
        profiles = work/"assets/ai"
        shutil.copytree(ROOT/"RedflagForge/assets/ai", profiles)
        shutil.copy2(args.animator.resolve(strict=True), profiles/"watt.animator.yaml")
    prepare(work)
    core = scripts/"Redflag-ScriptCore.dll"
    shutil.copy2(ROOT/"RedflagForge/Resources/Scripts/Redflag-ScriptCore.dll", core)
    vswhere = Path(os.environ["ProgramFiles(x86)"])/"Microsoft Visual Studio/Installer/vswhere.exe"
    install = subprocess.check_output([str(vswhere),"-latest","-property","installationPath"],text=True).strip()
    compiler = Path(install)/"MSBuild/Current/Bin/Roslyn/csc.exe"
    sources = sorted((ROOT/"SandboxProject/Source").rglob("*.cs"))
    subprocess.run([str(compiler),"/nologo","/target:library","/out:"+str(scripts/"Sandbox.dll"),
                    "/reference:"+str(core),*map(str,sources),str(ROOT/"tests/StationRecorder.cs"),
                    str(ROOT/"tests/StationMotionCapture.cs"),str(ROOT/"tests/StationLatency.cs")],check=True)
    source = ROOT/"RedflagForge/assets/scenes/Watt-Station.redflag"
    scene = yaml.safe_load(source.read_text(encoding="utf8"))
    if args.start_xz:
        player = next(entity for entity in scene["Entities"] if entity["Entity"] == 1101)
        position = player["TransformComponent"]["Translation"]
        position[0], position[2] = args.start_xz
    if args.orbit_yaw is not None:
        camera = next(entity for entity in scene["Entities"] if entity["Entity"] == 1100)
        camera["TransformComponent"]["Rotation"][1] = math.radians(args.orbit_yaw)
    scene["Entities"].append(dict(Entity=1900,TagComponent=dict(Tag="测试记录"),
        TransformComponent=dict(Translation=[0,0,0],Rotation=[0,0,0],Scale=[1,1,1]),
        ScriptComponent=dict(Class="StationTest.Recorder",ScriptFields=[
            dict(Name=name,Type="Entity",Value=value) for name,value in [("Player",1101),("Guard",1103),("Relay",1110),("Camera",1100)]])))
    if args.landing:
        scene["Entities"][-1]["ScriptComponent"]["ScriptFields"].append(dict(Name="RecordLanding",Type="Bool",Value=True))
    if args.melee or args.interaction:
        from station_melee import arrange
        arrange(scene, args.melee or "movement")
        if args.interaction:
            from station_interactions import place_targets
            place_targets(scene, args.interaction)
    if args.sentinel:
        from station_guard_gait import arrange
        arrange(scene)
    if args.watt_terrain:
        from station_npc_traversal import arrange
        arrange(scene,args.watt_terrain)
    if args.animation_actors:
        operator = next(row for row in scene["Entities"] if row["Entity"]==1101)
        camera = next(row for row in scene["Entities"] if row["Entity"]==1100)
        scene["Entities"] = [{key:value for key,value in camera.items() if key in
            ("Entity","TransformComponent","TagComponent","CameraComponent")}]
        scene["SkyVisible"] = False
        for uid, interrupt in ((1992,False),(1993,True)):
            scene["Entities"].append(dict(Entity=uid, TagComponent=dict(Tag="动画事件测试"),
                TransformComponent=dict(Translation=[0,-8,0],Rotation=[0,0,0],Scale=[1,1,1]),
                ModelRendererComponent=operator["ModelRendererComponent"], ModelAnimationComponent=operator["ModelAnimationComponent"],
                ScriptComponent=dict(Class="StationTest.NotifyActor",ScriptFields=[dict(Name="Interrupt",Type="Bool",Value=interrupt)])))
    if args.npc_motion or args.recorded_speech:
        if args.npc_motion and not args.recorded_speech:
            # 测「先捡再给」时让瓦特空手起步；已有一份药时模型可能直接选择交付。
            technician = next(row for row in scene["Entities"] if row["Entity"]==1102)
            technician["InventoryComponent"]["Items"].pop("medkit",None)
        scene["Entities"].append(dict(Entity=1901,TagComponent=dict(Tag="动作测试"),
            TransformComponent=dict(Translation=[0,0,0],Rotation=[0,0,0],Scale=[1,1,1]),
            ScriptComponent=dict(Class="StationTest.MotionCapture",ScriptFields=[
                dict(Name="Technician",Type="Entity",Value=1102),dict(Name="Player",Type="Entity",Value=1101)])))
    if args.recorded_speech:
        scene["Entities"][-1]["ScriptComponent"]["ScriptFields"].append(dict(Name="DeliverOnly",Type="Bool",Value=True))
        scene["Entities"] = [actor for actor in scene["Entities"] if actor["Entity"] != 1900]
    if args.latency:
        scene["Entities"] = [actor for actor in scene["Entities"] if actor["Entity"] != 1900]
        scene["Entities"].append(dict(Entity=1902, TagComponent=dict(Tag="NPC 计时"),
            TransformComponent=dict(Translation=[0,0,0],Rotation=[0,0,0],Scale=[1,1,1]),
            ScriptComponent=dict(Class="StationTest.Latency",ScriptFields=[
                dict(Name="Technician", Type="Entity", Value=1102),dict(Name="Count",Type="Int",Value=args.trials),
                dict(Name="Moving", Type="Bool", Value=args.latency=="walk"),
                dict(Name="DispatchOnly", Type="Bool", Value=args.latency=="dispatch")])))
    sample = work/"assets/scenes/Watt-Station.redflag"
    sample.write_text(yaml.safe_dump(scene,allow_unicode=True,sort_keys=False),encoding="utf8")
    shutil.copy2(str(source)+".render.yaml",str(sample)+".render.yaml")
    if args.animation_actors:
        Path(str(sample)+".render.yaml").write_text("Shading: Lit\nSamples: 1\nIndirect: Off\nTAA: false\n",encoding="utf8")
    if args.melee or args.interaction or args.sentinel or args.watt_terrain:
        Path(str(sample)+".render.yaml").write_text("Shading: Base color\n",encoding="utf8")
    output = ROOT/"output/validation/watt-station"
    output.mkdir(parents=True,exist_ok=True)
    name = "WattStation"+str(os.getpid())
    ctypes.windll.user32.SetProcessDPIAware()
    desktop = win32service.CreateDesktop(name,0,win32con.GENERIC_ALL,None)
    info = win32process.STARTUPINFO()
    info.lpDesktop = "WinSta0\\" + name
    command = subprocess.list2cmdline([str(exe),"--play",str(sample)])
    game_env = dict(os.environ,RF_VULKAN_VALIDATION="1")
    if args.recorded_speech: game_env["REDFLAG_PLAYTEST"] = str(work/"session")
    if args.melee or args.interaction or args.sentinel or args.watt_terrain or args.npc_motion:
        session = work/"session"
        session.mkdir()
        nodes = ["MASTER","Body","L-RearFoot","hand.L"] if args.sentinel else ["hand.L","foot.L","foot.R"]
        (session/"session.json").write_text(json.dumps({"pose_nodes":nodes}),encoding="utf8")
        game_env["REDFLAG_PLAYTEST"] = str(session)
    process, thread, pid, _ = win32process.CreateProcess(None, command, None, None, False, win32con.CREATE_NO_WINDOW,
                                    game_env,str(work),info)
    thread.Close()
    data = work/"station.csv"
    def latest():
        if not data.exists(): return None
        rows = list(csv.reader(data.read_text().splitlines()))
        return [float(v) for v in rows[-1]] if rows and len(rows[-1]) == 20 else None

    def result(test, seconds=20):
        def matches():
            row = latest()
            return row if row and test(row) else None
        try: return wait_for(matches,process,seconds)
        except RuntimeError as error: raise RuntimeError(f"{work}: {latest()}") from error

    def window():
        try:
            return next((int(w) for w in desktop.EnumDesktopWindows() if win32process.GetWindowThreadProcessId(w)[1] == pid
                        and win32gui.GetWindowText(w) == "Redflag Game"),None)
        except pywintypes.error: return None

    measurements = {}
    try:
        hwnd = wait_for(window,process)
        win32gui.PostMessage(hwnd,win32con.WM_SETFOCUS,0,0)
        if args.melee or args.interaction or args.sentinel or args.watt_terrain:
            win32gui.SetWindowPos(hwnd,None,0,0,840,540,win32con.SWP_NOMOVE|win32con.SWP_NOZORDER|win32con.SWP_NOACTIVATE)
            if args.watt_terrain:
                from station_npc_traversal import verify
                verify(args.watt_terrain,session,process,exe,ROOT/"output/validation/terrain-traversal/watt-follow",hwnd)
            elif args.sentinel:
                from station_guard_gait import verify
                verify(session,process,exe,ROOT/"output/validation/station-actions/sentinel.json")
            elif args.melee:
                from station_melee import verify
                verify(args.melee, session, process, exe, output/"melee")
            else:
                from station_interactions import verify
                verify(args.interaction,session,process,exe,output/"interactions")
            win32gui.PostMessage(hwnd,win32con.WM_CLOSE,0,0)
            assert win32event.WaitForSingleObject(process,25000)==win32con.WAIT_OBJECT_0 and win32process.GetExitCodeProcess(process)==0
            return
        if args.latency:
            # 两个游戏在相同的 640 × 360 客户区绘制，各自保留正式地图、渲染器和游戏时钟。
            left,top,wx,wy=win32gui.GetWindowRect(hwnd)
            _,_,cw,ch=win32gui.GetClientRect(hwnd)
            win32gui.SetWindowPos(hwnd,None,0,0,640+wx-left-cw,360+wy-top-ch,
                                  win32con.SWP_NOMOVE|win32con.SWP_NOZORDER|win32con.SWP_NOACTIVATE)
            record=work/"latency.tsv"
            def samples():
                if not record.exists(): return []
                return [row for row in csv.reader(record.read_text(encoding="utf8").splitlines(),delimiter="\t") if len(row)==6]
            wait_for(lambda: len(samples())>=args.trials,process,45+12*args.trials)
            _,_,cw,ch=win32gui.GetClientRect(hwnd)
            trials=[dict(trial=int(i),status=status,decision=choice,choice_ms=float(answer),action_ms=float(action),
                         observed_updates=int(frames)) for i,status,choice,answer,action,frames in samples()]
            if args.latency=="dispatch":
                trials=[dict(trial=t["trial"],status=t["status"],input_to_request_ms=t["choice_ms"],
                             observed_updates=t["observed_updates"]) for t in trials]
                assert all(t["status"]=="queued" for t in trials)
            target=ROOT/"output/validation/runtime-ai"/("forge-"+args.latency+"-latency.json")
            note=dict(scene=source.name,npc=1102,client=[cw,ch],exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
                      endpoint="local gateway", trials=trials)
            target.write_text(json.dumps(note,ensure_ascii=False,indent=2)+"\n",encoding="utf8")
            win32gui.PostMessage(hwnd,win32con.WM_CLOSE,0,0)
            assert win32event.WaitForSingleObject(process,25000)==win32con.WAIT_OBJECT_0
            print(target)
            return
        def key(k, down):
            code = ord(k) if isinstance(k,str) else k
            flags = 1 | (win32api.MapVirtualKey(code,0)<<16)
            win32gui.PostMessage(hwnd,win32con.WM_KEYDOWN if down else win32con.WM_KEYUP,code,flags if down else flags | (3<<30))
        def arrive(k, condition):
            key(k,True)
            try: return result(condition)
            finally: key(k,False)
        def tap(k):
            key(k,True); time.sleep(.23); key(k,False); time.sleep(.25)
        def frame(name):
            time.sleep(.4)
            save_view(hwnd,output/(name+".png"))
        if args.recorded_speech:
            sys.path.insert(0,str(ROOT/"scripts/redflag_mcp"))
            from playtest import issue
            directory = work/"session"
            media = directory/"media.jsonl"
            def frames():
                return [json.loads(row) for row in media.read_text(encoding="utf8").splitlines()] if media.exists() else []
            wait_for(lambda: (directory/"frames.jsonl").is_file() and (directory/"frames.jsonl").stat().st_size>0,process)
            issue(directory,1,"tap",key="T")  # 由当前拥有输入权的会话打开队友通讯。
            wait_for(lambda:any(r["event"]=="first_voice" for r in frames()),process,90)
            issue(directory,1,"tap",key="T")  # 收起通讯窗口会停止正在播的回执。
            wait_for(lambda:any(r["event"]=="stopped" for r in frames()),process,10)
            rows=frames()
            cut=next(r for r in rows if r["event"]=="stopped")
            assert cut["entity"]=="1102" and 0<cut["device_samples"]<((directory/cut["file"]).stat().st_size-44)//2
            target=args.recorded_speech.resolve()
            target.mkdir(parents=True,exist_ok=True)
            for file in (media,directory/cut["file"],work/"station-motion.tsv"): shutil.copy2(file,target)
            win32gui.PostMessage(hwnd,win32con.WM_CLOSE,0,0)
            assert win32event.WaitForSingleObject(process,25000)==win32con.WAIT_OBJECT_0
            print(target)
            return
        if args.animation_actors or args.landing:
            win32gui.SetWindowPos(hwnd,None,0,0,840,540,win32con.SWP_NOMOVE|win32con.SWP_NOZORDER|win32con.SWP_NOACTIVATE)
        if args.animation_actors:
            received = work/"notices.csv"
            wait_for(lambda: received.exists() and "KneeBend" in received.read_text() and "Recovery,End" in
                     "\n".join(line for line in received.read_text().splitlines() if line.startswith("1992,")),process)
            notices = list(csv.reader(received.read_text().splitlines()))
            completed = [r[2:] for r in notices if r[0]=="1992" and r[1]=="land"]
            cancelled = [r[2:] for r in notices if r[0]=="1993" and r[1]=="land"]
            animator = work/"assets/ai/watt.animator.yaml"
            states = yaml.safe_load(animator.read_text(encoding="utf-8"))["States"]
            events = next(state["Events"] for state in states if state["ID"]=="land")
            end = next(event["End"] for event in events if event["Name"]=="Recovery")
            assert [(r[0],r[1]) for r in completed] == [("Recovery","Begin"),("KneeBend","Point"),("Recovery","End")], notices
            assert abs(float(completed[-1][2])-end) < 1e-6 and completed[-1][-1]=="False", notices
            assert cancelled == [["Recovery","Begin","0","False"],["Recovery","End","0","True"]], notices
            target = ROOT/"output/validation/animation-events"
            target.mkdir(parents=True,exist_ok=True)
            note=dict(exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(), end=end, script_notices=notices)
            (target/(args.name+".json")).write_text(json.dumps(note,indent=2)+"\n",encoding="utf8")
            win32gui.PostMessage(hwnd,win32con.WM_CLOSE,0,0)
            assert win32event.WaitForSingleObject(process,25000)==win32con.WAIT_OBJECT_0 and win32process.GetExitCodeProcess(process)==0
            print(json.dumps(note))
            return
        measurements["start"] = result(lambda r:r[0] > (0 if args.npc_motion else 2),70)
        if args.landing:
            received = work/"landing.csv"
            key(win32con.VK_SPACE,True)
            try:
                result(lambda row: row[15] == 1)
            finally:
                key(win32con.VK_SPACE,False)
            wait_for(lambda: received.exists() and ",End," in received.read_text(), process)
            rows = list(csv.reader(received.read_text().splitlines()))
            assert [row[1] for row in rows] == ["Begin","End"], rows
            target = ROOT/"output/validation/animation-events"
            target.mkdir(parents=True,exist_ok=True)
            animator = work/"assets/ai/watt.animator.yaml"
            states = yaml.safe_load(animator.read_text(encoding="utf-8"))["States"]
            events = next(state["Events"] for state in states if state["ID"]=="land")
            recovery = next(event for event in events if event["Name"]=="Recovery")
            duration = float(rows[1][0])-float(rows[0][0])
            # C# 每帧观察一次，端点均有一帧的时间误差。
            assert abs(duration-(recovery["End"]-recovery["Start"])) <= sum(float(row[2]) for row in rows)+.002, rows
            note=dict(exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),configured=recovery,
                      callbacks=rows,observed_seconds=duration)
            (target/(args.name+".json")).write_text(json.dumps(note,indent=2)+"\n",encoding="utf8")
            win32gui.PostMessage(hwnd,win32con.WM_CLOSE,0,0)
            assert win32event.WaitForSingleObject(process,25000)==win32con.WAIT_OBJECT_0 and win32process.GetExitCodeProcess(process)==0
            print(json.dumps(note))
            return
        if args.npc_motion:
            events = work/"station-motion.tsv"
            pictures = output/args.name
            pictures.mkdir(exist_ok=False)
            seen = set()
            shots = {}
            deadline = time.monotonic()+125
            while time.monotonic() < deadline:
                if events.exists():
                    for line in events.read_text(encoding="utf-8-sig").splitlines():
                        cells = line.split("\t")
                        if len(cells)!=6: continue
                        event_time, kind, action, target, _, _ = cells
                        if kind == "动作" and action in ("walk","pickup","give"):
                            stage = action
                        elif kind == "库存":
                            stage = "collected" if target == "1112" else "delivered"
                        else:
                            continue
                        if stage not in seen:
                            save_view(hwnd,pictures/(stage+".png"))
                            shots[stage] = {"event_seconds":float(cells[0]),"last_recorded_frame_seconds":latest()[0]}
                            seen.add(stage)
                        if kind == "动作" and action in ("pickup", "give"):
                            pose = stage+"-pose"
                            moment = float(event_time) + (.85 if action == "pickup" else .55)
                            if pose not in seen and moment <= latest()[0] < moment + .4:
                                save_view(hwnd,pictures/(pose+".png"))
                                shots[pose] = {"event_seconds":float(event_time),"last_recorded_frame_seconds":latest()[0]}
                                seen.add(pose)
                    if "delivered" in seen: break
                time.sleep(.06)
            if events.exists(): shutil.copy2(events,pictures/"events.tsv")
            (pictures/"screens.json").write_text(json.dumps(shots,indent=2)+"\n",encoding="utf8")
            if "delivered" not in seen:
                raise RuntimeError(f"物品尚未交给玩家。实际动作记录在 {pictures}")
            from station_companion_pose import verify
            verify(session, pictures/"pose.json")
        else:
            if args.show_help:
                key(win32con.VK_F1, True)
                first_frame = latest()[0]
                result(lambda row: row[0] > first_frame + .5)
            frame(args.name)
            if args.show_help:
                key(win32con.VK_F1, False)
        if args.preview or args.npc_motion:
            win32gui.PostMessage(hwnd,win32con.WM_CLOSE,0,0)
            if win32event.WaitForSingleObject(process,25000) != win32con.WAIT_OBJECT_0 or win32process.GetExitCodeProcess(process) != 0:
                raise RuntimeError("Game failed to stop")
            print(output/args.name if args.npc_motion else output/(args.name+".png"))
            return
        arrive("W",lambda r:r[1]>-4.05)
        measurements["collect"] = arrive("E", lambda r:r[6] == 2)
        # 甲板前侧避开电网，再靠近巡检机。角色需要发出三次有效脉冲才能解除威胁。
        arrive("D",lambda r:r[2]>2.85)
        key(win32con.VK_SPACE,True)
        arrive("W",lambda r:r[1]>.65)
        measurements["guard"] = result(lambda r:r[4] == 0)
        key(win32con.VK_SPACE,False)
        arrive("D",lambda r:r[2]>3.9)
        arrive("W",lambda r:r[1]>6.7)
        arrive("A",lambda r:r[2]<2.83)
        measurements["repair"] = arrive("E", lambda r:r[5] == 100)
        frame("power")
        if latest()[1] < 6.1:
            arrive("A",lambda r:r[2]<-1.70)
        if latest()[2] > -1.7:
            arrive("S",lambda r:r[1]<4.4)
            arrive("A",lambda r:r[2]<-1.7)
        measurements["exit"] = arrive("S",lambda r:r[1]<-6.46)
        frame("exit")
        tap("R")
        measurements["restart"] = result(lambda r:r[1]<-6.8 and r[5]==0 and r[4]==75 and r[6]==1)
        # 第二局站在巡检机前，记录生命耗尽及十二秒救援倒计时后的画面。
        arrive("W",lambda r:r[1] > -1.5)
        measurements["down"] = result(lambda r:r[3] == 0,40)
        frame("down")
        # 队友是否赶得及取决于本机模型。允许实际救援，等待游戏正式停止更新。
        deadline = time.monotonic()+70
        observed = latest()
        while time.monotonic() < deadline:
            time.sleep(1.25)
            row = latest()
            if row == observed and row[3] == 0: break
            observed = row
        else: raise RuntimeError("The match has not ended")
        frame("defeat")
        win32gui.PostMessage(hwnd,win32con.WM_CLOSE,0,0)
        if win32event.WaitForSingleObject(process,25000) != win32con.WAIT_OBJECT_0 or win32process.GetExitCodeProcess(process) != 0:
            raise RuntimeError("Game failed to stop")
    finally:
        if win32process.GetExitCodeProcess(process)==259: win32process.TerminateProcess(process,1)
        process.Close()
        desktop.CloseDesktop()
    shutil.copy2(data,output/"play.csv")
    report = dict(exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),fields=["seconds","x","z","player_health",
                  "guard_health","relay_health","medkits"],measured=measurements)
    (output/"play.json").write_text(json.dumps(report,indent=2)+"\n",encoding="utf8")
    print(json.dumps(report))


if __name__=="__main__":
    main()
