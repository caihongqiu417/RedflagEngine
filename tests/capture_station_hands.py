"""从发行包拍维修站双人交互。固定观察镜头与玩家瞄准相机分开，物品、生命与动作仍由游戏推进。"""
import argparse
import copy
import ctypes
import gzip
import shutil
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
import yaml

from capture_player_motion import NODES, digest, until
from playtest import begin, current, issue, write_json
from playtest_capture import client_image

VERBS=("pickup","give","repair","revive","heal")
INTENTS=dict(pickup="gather",give="share",repair="repair",revive="revive",heal="heal")
SAY=dict(pickup="请去捡地上的医疗包，收进你自己背包。",give="请把你背包里的一份医疗包亲手送到我手上。",
         repair="请去修好旁边的应急发电机。",revive="我倒下了，过来把我扶起来。",heal="你受伤了。用你自己背包里的医疗包治疗你自己，不要交给我。")
KEYS=dict(pickup="E",give="G",repair="E",revive="E",heal="H")
PLAYER, WATT, RELAY, BOX = "1101","1102","1110","1112"


def stage(scene,who,action,camera_view):
    entities={str(e["Entity"]):e for e in scene["Entities"]}
    before=copy.deepcopy(entities)
    npc=who=="watt"
    def place(ident,x,z,yaw=None):
        tr=entities[ident]["TransformComponent"]
        tr["Translation"]=[x,.08,z]
        if yaw is not None:tr["Rotation"][1]=yaw
    mover=WATT if npc else PLAYER
    other=PLAYER if npc else WATT
    place(mover,-6.9,1.4,math.pi/2)
    place(other,-3.25,1.4,-math.pi/2) if action in ("give","revive") else place(other,-12.2,1.4,-math.pi/2)
    if action=="pickup":
        place(BOX,-3.25,2.02)
        if "1921" in entities: entities[BOX]["TransformComponent"]["Translation"][1] = .13
    else: place(BOX,-2.5,4.0)
    if "1921" in entities:place("1921",-6.0,3.45)  # 旧工具箱留下作布景，别让它遮住地上的小包。
    if action=="repair":place(RELAY,-3.25,1.4)
    if action=="heal":entities[mover]["HealthComponent"]["Current"]=40
    if action=="revive":entities[other]["HealthComponent"]["Current"]=0
    # 留下实际瞄准与射线的相机。在旁边加一个只负责输出画面的相机，拍清谁伸手。
    camera=entities["1100"]
    camera["CameraComponent"]["Primary"]=False
    if action=="pickup" and not npc: camera["TransformComponent"]["Rotation"][0]=-.68
    spectator=dict(Entity=1999991,TagComponent={"Tag":"侧面验证镜头"},
        CameraComponent=copy.deepcopy(camera["CameraComponent"]),
        TransformComponent=dict(Translation=[-4.49,1.52,-2.75],Rotation=[-.13,math.pi,0],Scale=[1,1,1]))
    if camera_view=="yard":
        center=-6.9 if npc and action=="heal" else -5.5 if action=="heal" else -4.3
        spectator["TransformComponent"].update(Translation=[center+1.55,1.45,4.92],Rotation=[-.16,.40,0])
    spectator["CameraComponent"]["Primary"]=True
    spectator["CameraComponent"]["Camera"]["PerspectiveFov"]=.80
    scene["Entities"].append(spectator)
    changes={ident:{k:v for k,v in entities[ident].items() if before[ident].get(k)!=v}
             for ident in entities if before[ident]!=entities[ident]}
    return dict(entities=changes,spectator=spectator),mover,other


def play(project,archive,scene_file,npc_file,who,action,output,routine,camera_view,original):
    output.mkdir(parents=True,exist_ok=False)
    images=output/"images";images.mkdir()
    scene=yaml.safe_load(scene_file)
    changes,mover,other=stage(scene,who,action,camera_view)
    path=project/"assets/scenes/Watt-Station.redflag"
    if original: path.write_bytes(scene_file);changes={}
    else: path.write_text(yaml.safe_dump(scene,allow_unicode=True,sort_keys=False),encoding="utf8")
    npc=yaml.safe_load(npc_file)
    # 定点拍摄时，未受指令的瓦特要留在标记处。另可按作者公开的 IdleIntent 启动指定的演出动作。
    npc["IdleIntent"]=INTENTS[action] if who=="watt" and routine else "wait"
    role=project/"assets/ai/watt.npc.yaml"
    if original: role.write_bytes(npc_file);npc=yaml.safe_load(npc_file)
    else: role.write_text(yaml.safe_dump(npc,allow_unicode=True,sort_keys=False),encoding="utf8")
    test=begin(project,"assets/scenes/Watt-Station.redflag",f"{who} {action} 接触与恢复",launch=False)
    session=project/"captures/playtests"/test["session"]
    test["pose_nodes"]=NODES+["palm.L","palm.R","head.001","cannon.R"]
    write_json(session/"session.json",test)
    name=f"StationHands{os.getpid()}"
    desk=win32service.CreateDesktop(name,0,win32con.GENERIC_ALL,None)
    startup=win32process.STARTUPINFO();startup.lpDesktop="WinSta0\\"+name
    cmd=[str(project/"RedflagForge.exe"),"--vulkan","--play",str(path),"--render-size","1280x720"]
    proc,thread,pid,_=win32process.CreateProcess(None,subprocess.list2cmdline(cmd),None,None,False,
        win32con.CREATE_NO_WINDOW,dict(os.environ,REDFLAG_PLAYTEST=str(session)),str(project),startup)
    thread.Close()
    recorded=[]; tickets=[]
    def send(op,key="",**args):
        if op=="hold":args["lease_ms"]=8000
        c=issue(session,1,op,key,**args);tickets.append(c)
    try:
        def window():
            try: handles=desk.EnumDesktopWindows()
            except pywintypes.error:return None
            if win32process.GetExitCodeProcess(proc)!=259:raise RuntimeError("游戏尚未显示窗口就退出")
            return next((int(h) for h in handles if win32process.GetWindowThreadProcessId(h)[1]==pid
                         and win32gui.GetWindowText(h)=="Redflag Game"),None)
        hwnd=until(window)
        until(lambda:current(session)["frame"])
        last=-1;phase=0;at_time=0.;at_seq=0;completed=None;stopped=False;resume=False;finish=False
        initial=None;origin=None;dialog=who=="watt" and not routine and action!="revive"
        while win32process.GetExitCodeProcess(proc)==259:
            frame=current(session)["frame"];seq=frame["seq"]
            if seq==last:time.sleep(.004);continue
            last=seq
            actors={a["id"]:a for a in frame["actors"]};owner=actors[mover];dst=actors[other]
            now=frame["seconds"]
            if initial is None:initial=actors;origin=now
            if seq<=7:origin=now
            kits=lambda a:a.get("inventory",{}).get("medkit",0)
            if action=="pickup":success=BOX not in actors and kits(owner)>kits(initial[mover])
            elif action=="give":success=kits(dst)>kits(initial[other]) and kits(owner)<kits(initial[mover])
            elif action=="heal":success=owner["health"]>initial[mover]["health"] and kits(owner)<kits(initial[mover])
            elif action=="revive":success=dst["health"]>0
            else:success=actors[RELAY]["health"]>0
            if success and completed is None:completed=seq;completed_at=now
            def next_phase():
                nonlocal phase,at_seq,at_time
                phase+=1;at_time=now;at_seq=seq
            if who=="player":
                if phase==0 and seq>=7:send("hold","W");next_phase()
                elif phase==1 and owner["position"][0]>=(-5.7 if action=="heal" else -5.04 if action=="pickup" else -4.65):
                    send("release","W");next_phase()
                elif phase==2 and action=="pickup" and now-at_time>.65:
                    # 从第三人称相机的肩位瞄准小药包，拖动真实鼠标调整俯仰。停步位置随帧率而变。
                    box=actors[BOX]["position"]; foot=owner["position"]
                    aim=math.atan2(box[1]-foot[1]-1.38,box[0]-foot[0])
                    drag=round((-.68-aim)/.0045)
                    send("move",x=960,y=500);send("hold","MouseRight");next_phase()
                elif phase==3 and action=="pickup" and seq-at_seq>=3:
                    send("move",x=960,y=500+drag);next_phase()
                elif phase==4 and action=="pickup" and now-at_time>.48:
                    send("release","MouseRight");next_phase()
                elif phase==5 and action=="pickup" and now-at_time>.35:
                    send("tap","E");next_phase()
                elif phase==2 and action!="pickup" and now-at_time>.30:
                    send("hold" if action in ("repair","revive") else "tap",KEYS[action]);next_phase()
                elif phase==3 and action in ("repair","revive") and not stopped:
                    if completed:send("release","E");stopped=True
                    elif now-at_time>=5:send("hold","E");at_time=now
            elif dialog and not success:
                if phase==0 and seq>=7:send("tap","T");next_phase()
                elif phase==1 and seq-at_seq>=4:send("move",x=195,y=310);next_phase()
                elif phase==2 and seq-at_seq>=3:send("tap","MouseLeft");next_phase()
                elif phase==3 and seq-at_seq>=4:
                    for char in SAY[action]:win32gui.PostMessage(hwnd,win32con.WM_CHAR,ord(char),1)
                    next_phase()
                elif phase==4 and seq-at_seq>=3:send("tap","Enter");next_phase()
                elif phase==5 and seq-at_seq>=5:send("tap","T");next_phase()
            # 连续记录至动作、状态变化与收手之后。玩家再走几步，验证操作已经交还。
            if completed and now-completed_at>2.0 and not resume:
                if who=="player":send("hold","S")
                resume=True;resume_t=now
            if resume and now-resume_t>.48 and not finish:
                if who=="player":send("release","S")
                finish=True;end_t=now
            picture=client_image(hwnd);end_seq=current(session)["frame"]["seq"]
            picture.thumbnail((1280,720));picture.save(images/f"{seq:05}.jpg",quality=91)
            recorded.append(dict(seq=seq,seq_end=end_seq,seconds=now))
            # NPC 的行动回执有时在收手后才交给声卡。留到实际播完再结束游戏。
            heard=True
            if who=="watt" and completed and now-completed_at<7:
                media=session/"media.jsonl"
                records=[json.loads(line) for line in media.read_bytes().splitlines() if line.endswith(b"}")] if media.exists() else []
                said=any(row.get("event")=="finished" for row in records if row.get("seq",0)>=completed)
                heard=said or now-completed_at>=6
            if (finish and now-end_t>.55 and heard) or now-origin>(30 if dialog else 17):break
        send("end")
        assert win32event.WaitForSingleObject(proc,20000)==win32con.WAIT_OBJECT_0
    finally:
        if win32process.GetExitCodeProcess(proc)==259:
            win32process.TerminateProcess(proc,1);win32event.WaitForSingleObject(proc,20000)
        proc.Close()
        desk.CloseDesktop()
    # 只保留这局真正提交给声音设备的文件。回放按 stopped/finished 的样本数截短。
    for pattern in ("media.jsonl","speech-*.wav","decisions.jsonl"):
        for recording in session.glob(pattern):shutil.copy2(recording,output/recording.name)
    frames=(session/"frames.jsonl").read_bytes()
    (output/"frames.jsonl.gz").write_bytes(gzip.compress(frames,mtime=0))
    result=dict(actor=who,action=action,archive_sha256=digest(archive),exe_sha256=digest(project/"RedflagForge.exe"),
        scene=changes,camera_view="package original" if original else camera_view,npc_idle=npc["IdleIntent"],instruction=SAY[action] if dialog else None,
        trigger="NPC RescueIntent" if who=="watt" and action=="revive" else
        ("actor IdleIntent" if who=="watt" and routine else "game T chat" if dialog else "player keyboard"),
        completed=completed,captures=recorded,inputs=tickets)
    write_json(output/"take.json",result)
    print(who,action,"completed",completed,"captured",len(recorded),flush=True)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("archive",type=Path);p.add_argument("output",type=Path)
    p.add_argument("--cases",nargs="+",default=[f"{who}-{action}" for who in ("player","watt") for action in VERBS])
    p.add_argument("--routine",action="store_true",help="瓦特以作者配置的 IdleIntent 起步，不用对话服务")
    p.add_argument("--view",choices=("side","yard"),default="yard")
    p.add_argument("--original",action="store_true",help="保留发行包中整份场景与角色配置；适合瓦特递药用例")
    a=p.parse_args();ctypes.windll.user32.SetProcessDPIAware()
    with tempfile.TemporaryDirectory(prefix="station-hands-") as tmp:
        project=Path(tmp)
        with zipfile.ZipFile(a.archive) as z:z.extractall(project)
        scene=(project/"assets/scenes/Watt-Station.redflag").read_bytes()
        npc=(project/"assets/ai/watt.npc.yaml").read_bytes()
        for item in a.cases:
            who,action=item.split("-")
            if who not in ("player","watt") or action not in VERBS:p.error("未知的交互用例")
            play(project,a.archive,scene,npc,who,action,a.output/item,a.routine,a.view,a.original)


if __name__=="__main__":main()
