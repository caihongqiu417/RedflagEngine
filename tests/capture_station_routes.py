"""从未改动的游戏包步行穿过货场。按实际窗口的呈现时间录视频，保存巡检机的逐帧观察。"""
import argparse
import ctypes
import json
import math
import os
from pathlib import Path
import subprocess
import tempfile
import time
import zipfile

import pywintypes
import win32con
import win32event
import win32gui
import win32process
import win32service

from capture_player_motion import begin, client_image, current, digest, issue, until
from playtest import frames


def video(images, destination):
    manifest = images[0][1].parent/"frames.ffconcat"
    lines = ["ffconcat version 1.0"]
    for index,(seconds,image) in enumerate(images):
        duration = images[index+1][0]-seconds if index+1<len(images) else .15
        lines += [f"file '{image.name}'",f"duration {duration:.6f}"]
    lines.append(f"file '{images[-1][1].name}'")
    manifest.write_text("\n".join(lines),encoding="ascii")
    subprocess.run(["ffmpeg","-loglevel","error","-y","-f","concat","-safe","0","-i",str(manifest),
                    "-vf","fps=24","-pix_fmt","yuv420p","-c:v","libx264","-preset","fast","-crf","20",
                    "-movflags","+faststart",str(destination)],check=True)


def play(archive,project,output,route):
    output.mkdir(parents=True,exist_ok=False)
    pictures=project/("frames-"+route)
    pictures.mkdir()
    scene="assets/scenes/Watt-Station.redflag"
    start=begin(project,scene,"从西向东走中央通道或绕北面货柜",launch=False)
    session=project/"captures/playtests"/start["session"]
    exe=project/"RedflagForge.exe"
    command=subprocess.list2cmdline([str(exe),"--vulkan","--play",scene,"--render-size","1280x720"])
    name=f"RedflagRoute{os.getpid()}{route}"
    desktop=win32service.CreateDesktop(name,0,win32con.GENERIC_ALL,None)
    startup=win32process.STARTUPINFO()
    startup.lpDesktop="WinSta0\\"+name
    proc,thread,pid,_=win32process.CreateProcess(None,command,None,None,False,win32con.CREATE_NO_WINDOW,
        dict(os.environ,REDFLAG_PLAYTEST=str(session)),str(project),startup)
    thread.Close()

    def window():
        try: handles=desktop.EnumDesktopWindows()
        except pywintypes.error: return None
        return next((int(h) for h in handles if win32process.GetWindowThreadProcessId(h)[1]==pid
                     and win32gui.GetWindowText(h)=="Redflag Game"),None)

    # 绕行先从西货柜的背面进入北道，在东货柜后面折向发电机。区间为场景世界坐标（米）。
    central=[("W","encounter",None),("","observe",0),("W","east",11.08)]
    north=[("S","west",-8.87),("A","cover",-6.95),("W","east",11.05),("D","generator",1.21)]
    stages=central if route=="central" else north
    samples=[]; events=[]; shots=[]; tickets=[]; transitions=[]; saved=set()
    begin_at=None; stop_reason=""; offset=0; stage=-1; since=0.; furthest=0.; progress_at=0.
    held=""; guard=""; target=""; last_picture=-99.; recent=None; last_renew=0.

    def send(op,key=""):
        options=dict(key=key)
        if op=="hold":options["lease_ms"]=10000
        ticket=issue(session,1,op,**options)
        tickets.append(ticket)

    def advance(now):
        nonlocal held,stage,since,progress_at,furthest,last_renew
        if held: send("release",held)
        stage+=1; since=now; progress_at=now; furthest=0.; held=""
        if stage<len(stages):
            held=stages[stage][0]
            if held:send("hold",held)
            last_renew=time.monotonic()
            transitions.append(dict(seconds=now,stage=stages[stage][1],position=recent))
            print(route,stages[stage][1],recent,round(now,2),flush=True)

    try:
        hwnd=until(window)
        until(lambda:(current(session).get("frame") or {}).get("seq",0)>=5)
        deadline=time.monotonic()+120
        while not stop_reason and time.monotonic()<deadline:
            batch=frames(session,offset,600); offset=batch["offset"]
            if not batch["frames"]:time.sleep(.009);continue
            special=[]; fired=False
            for frame in batch["frames"]:
                actors={a["id"]:a for a in frame["actors"]}
                if "1101" not in actors or "1103" not in actors:continue
                if begin_at is None:begin_at=frame["seconds"]
                now=frame["seconds"]-begin_at
                player=actors["1101"]; enemy=actors["1103"]; recent=player["position"]
                for event in frame["events"]:
                    if event["kind"] not in ("sentinel","sentinel_target"):continue
                    value=event["text"]
                    if event["kind"]=="sentinel":
                        guard=value
                        if value in ("charge","discharge","search"): special.append(value)
                        if value=="discharge":fired=True
                    else:target=value
                    events.append(dict(seq=frame["seq"],seconds=now,kind=event["kind"],value=value,
                                       player=recent,enemy=enemy["position"]))
                samples.append(dict(seq=frame["seq"],seconds=now,player=recent,health=player["health"],
                                    enemy=enemy["position"],action=guard,target=target))
            if not samples:continue
            sample=samples[-1]; now=sample["seconds"];seq=sample["seq"];x,_,z=recent
            if not saved:special.append("start")
            if stage<0:advance(now)
            else:
                key,step,end=stages[stage]
                done=((step=="encounter" and (guard in ("charge","discharge","cool") or x>=3.7))
                      or (step=="observe" and (fired or now-since>4.5))
                      or (step=="west" and x<=end) or (step=="east" and x>=end)
                      or (step=="cover" and z<=end) or (step=="generator" and z>=end))
                if done:
                    special.append(step);advance(now)
                    if stage==len(stages):stop_reason="arrived"
                elif held:
                    if time.monotonic()-last_renew>6:
                        send("hold",held);last_renew=time.monotonic()
                    distance=math.dist(recent,transitions[-1]["position"])
                    if distance>furthest+.14:
                        progress_at=now;furthest=distance
                    if now-progress_at>4.:
                        special.append("blocked");stop_reason="blocked at "+step
            if sample["health"]<=0:stop_reason="down"
            if now-last_picture>=.10 or special:
                picture=client_image(hwnd);picture.thumbnail((1280,720))
                after=current(session)["frame"]["seq"]
                stamp=time.monotonic()
                path=pictures/f"{seq:07}.jpg";picture.save(path,quality=90)
                shots.append((stamp,path));last_picture=now
                for key in set(special)-saved:
                    picture.save(output/(key+".png"))
                    saved.add(key)
                    samples[-1].setdefault("screenshots",[]).append(dict(name=key,from_seq=seq,to_seq=after))
        if not stop_reason:stop_reason="timeout"
        if held:send("release",held)
        if win32process.GetExitCodeProcess(proc)==259:issue(session,1,"end")
        if win32event.WaitForSingleObject(proc,25000)!=win32con.WAIT_OBJECT_0:raise TimeoutError("游戏未退出")
    finally:
        if win32process.GetExitCodeProcess(proc)==259:win32process.TerminateProcess(proc,1)
        proc.Close();desktop.CloseDesktop()
    receipt=[json.loads((session/"receipts"/f"{t['id']:06}.json").read_bytes()) for t in tickets]
    assert all(r["ok"] for r in receipt),receipt
    walking=[s for s in samples if s["seconds"]>=transitions[0]["seconds"]]
    duration={}; targeted={}
    for a,b in zip(walking,walking[1:]):
        dt=b["seconds"]-a["seconds"]
        duration[a["action"]]=duration.get(a["action"],0.)+dt
        if a["target"]:targeted[a["target"]]=targeted.get(a["target"],0.)+dt
    goal=(12.2,.08,1.4)
    stats=dict(seconds=walking[-1]["seconds"]-walking[0]["seconds"],
               path_metres=sum(math.dist(a["player"],b["player"]) for a,b in zip(walking,walking[1:])),
               metres_from_generator=math.dist(walking[-1]["player"],goal),ending_health=walking[-1]["health"],
               guard_action_seconds=duration,guard_target_seconds=targeted,captured_images=len(shots))
    report=dict(archive=archive.name,archive_sha256=digest(archive),exe_sha256=digest(exe),
                scene_sha256=digest(project/scene),render_size="1280x720",route=route,result=stop_reason,
                origin_seconds=begin_at,statistics=stats,transitions=transitions,commands=tickets,events=events,samples=samples)
    (output/"route.json").write_text(json.dumps(report,ensure_ascii=False,separators=(",",":")),encoding="utf8")
    video(shots,output/"route.mp4")
    print(route,stop_reason,recent,len(shots),"images",flush=True)
    assert stop_reason=="arrived" and stats["metres_from_generator"]<1.35,stats
    if route=="north":
        assert min(s["player"][2] for s in walking)<-6.5,stats


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive",type=Path)
    parser.add_argument("output",type=Path)
    parser.add_argument("--route",choices=["central","north","both"],default="both")
    args=parser.parse_args()
    ctypes.windll.user32.SetProcessDPIAware()
    with tempfile.TemporaryDirectory(prefix="redflag-yard-") as folder:
        project=Path(folder)
        with zipfile.ZipFile(args.archive) as z:z.extractall(project)
        for route in (["central","north"] if args.route=="both" else [args.route]):
            play(args.archive.resolve(),project,args.output.resolve()/route,route)
