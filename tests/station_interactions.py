"""维修站动作回归。场景副本只固定目标位置、初始生命和镜头，原关卡脚本执行交互。"""
import hashlib
import json
from check_forge_play import wait_for
from playtest import issue


def place_targets(scene,case):
    entities={row["Entity"]:row for row in scene["Entities"]}
    if case=="pickup":
        entities[1100]["TransformComponent"]["Rotation"][0]=-.785398
        entities[1112]["TransformComponent"]["Translation"]=[-5.58,.14,2.02]
        entities[1101]["HealthComponent"]["Current"]=40
    if case=="repair": entities[1110]["TransformComponent"]["Translation"]=[-5.4,.08,1.4]
    if case=="revive":
        entities[1102]["NpcComponent"]={"Definition":"assets/ai/watt.npc.yaml","Leader":1101}
        entities[1102]["TransformComponent"]["Translation"]=[-5.4,.08,1.4]
        entities[1102]["HealthComponent"]["Current"]=0
    if case=="equipment": entities[1103]["TransformComponent"]["Translation"]=[-4,.08,2.02]


def verify(case,session,process,exe,output):
    stream=session/"frames.jsonl"
    samples=[]

    def frames():
        if not stream.exists(): return []
        return [json.loads(row) for row in stream.read_bytes().split(b"\n")[:-1]]

    def actor(row,idx=1101):
        return next(a for a in row["actors"] if a["id"]==str(idx))

    def event(row,name):
        return next((e for e in row["events"] if e["kind"]==name),None)

    def key(op,name):
        issue(session,1,op,key=name,lease_ms=20000 if op=="hold" else None)

    def until(title,condition,after=0):
        def find():
            return next((r for r in frames() if r["seq"]>after and condition(r)),None)
        row=wait_for(find,process,20)
        player=actor(row)
        samples.append(dict(step=title,frame=row["seq"],seconds=row["seconds"],clip=player["animation"]["clip"],
                            clip_time=player["animation"]["time"],player_health=player["health"],
                            hand=player["animation"]["nodes"]["hand.L"],kits=player["inventory"].get("medkit",0),
                            relay=actor(row,1110)["health"],watt=actor(row,1102)["health"],
                            guard=actor(row,1103)["health"],contact=player.get("contact"),events=row["events"]))
        return row

    start=until("ready",lambda r:r["seq"]>=6 and r["seconds"]>=.8)
    if case=="pickup":
        key("tap","E")
        begin=until("reach",lambda r:actor(r)["animation"]["clip"]=="Station-Pickup",start["seq"])
        middle=until("before contact",lambda r:actor(r)["animation"]["time"]>=.85,begin["seq"])
        assert actor(middle)["inventory"]["medkit"]==1
        contact=until("contact",lambda r:event(r,"pickup_contact"),middle["seq"])
        prev=next(r for r in frames() if r["seq"]==contact["seq"]-1)
        assert 1.19<=actor(prev)["animation"]["time"]<=1.20+prev["frame_ms"]/1000+.01
        assert actor(prev)["inventory"]["medkit"]==1 and actor(contact)["inventory"]["medkit"]==2
        assert actor(contact)["contact"]["commit_distance"] <= .04
        assert actor(contact)["contact"]["prop"] != "0"
        assert event(contact,"pickup_contact")["text"]=="1112" and not any(a["id"]=="1112" for a in contact["actors"])
        returned=until("stand",lambda r:actor(r)["animation"]["clip"]=="Idle-cycle",contact["seq"])
        key("hold","H")
        open_kit=until("raise medkit",lambda r:event(r,"heal_begin"),returned["seq"])
        assert actor(open_kit)["animation"]["clip"]=="Station-UseMedkit"
        assert actor(open_kit)["health"]==40 and actor(open_kit)["inventory"]["medkit"]==2
        healed=until("heal",lambda r:event(r,"heal_contact"),open_kit["seq"])
        assert actor(healed)["health"]==90 and actor(healed)["inventory"]["medkit"]==1
        wait=until("still holding H after stowing",lambda r:actor(r)["animation"]["clip"]=="Idle-cycle",healed["seq"])
        assert actor(wait)["inventory"]["medkit"]==1 and actor(wait)["health"]==90
        key("release","H")
    elif case in ("repair","revive"):
        target=1110 if case=="repair" else 1102
        clip="Station-Repair" if case=="repair" else "Station-Revive"
        key("hold","E")
        begin=until("start",lambda r:actor(r)["animation"]["clip"]==clip,start["seq"])
        if case=="repair":
            midway=until("partial",lambda r:r["seconds"]>begin["seconds"]+.65,begin["seq"])
            key("release","E")
            stopped=until("released",lambda r:actor(r)["animation"]["clip"]=="Idle-cycle",midway["seq"])
            assert actor(stopped,target)["health"]==0
            key("hold","E")
            begin=until("restart",lambda r:actor(r)["animation"]["clip"]==clip,stopped["seq"])
        ok=until("completed",lambda r:actor(r,target)["health"]>0,begin["seq"])
        assert actor(ok)["contact"]["commit_distance"] <= .04
        assert actor(ok,target)["health"]==(100 if case=="repair" else 50)
        assert 2.-begin["frame_ms"]/1000-.01<=ok["seconds"]-begin["seconds"]<=2.+ok["frame_ms"]/1000+.1
        key("release","E")
    else:
        issue(session,1,"move",x=410,y=250)
        key("tap","2")
        switch=until("equip scanner",lambda r:actor(r)["animation"]["clip"]=="Cannon_activation-noexp",start["seq"])
        ready=until("scanner ready",lambda r:actor(r)["animation"]["clip"]=="Idle-cycle",switch["seq"])
        key("tap","MouseLeft")
        scan=until("scan",lambda r:event(r,"scan"),ready["seq"])
        assert event(scan,"scan")["text"]=="1103" and actor(scan,1103)["health"]==75
        key("tap","1")
        switch=until("equip cannon",lambda r:actor(r)["animation"]["clip"]=="Cannon_activation-noexp",scan["seq"])
        ready=until("cannon ready",lambda r:actor(r)["animation"]["clip"]=="Idle-cycle",switch["seq"])
        key("hold","MouseRight")
        aim=until("aim",lambda r:actor(r)["animation"]["clip"]=="AIM-Center",ready["seq"])
        key("tap","MouseLeft")
        shot=until("pulse",lambda r:event(r,"pulse"),aim["seq"])
        assert actor(shot)["animation"]["clip"]=="Cannon_Charge" and actor(shot,1103)["health"]==50
        key("release","MouseRight")
    output.mkdir(parents=True,exist_ok=True)
    path=output/(case+".json")
    path.write_text(json.dumps(dict(exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),case=case,samples=samples),
                               indent=2,ensure_ascii=False)+"\n",encoding="utf8")
    print(path)
