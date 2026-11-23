"""正式瓦特在测试平台跟随玩家。脚本只给领队按键，瓦特自己调用 NpcSystem::Approach。"""
import csv
import hashlib
import json
import math

from check_forge_play import wait_for
from check_traversal_play import platform, transform
from playtest import issue
from profile_native_navigation import save_view


def arrange(scene,case):
    entities={row["Entity"]:row for row in scene["Entities"]}
    keep=(1100,1101,1102,1103,1110,1111,1112)
    scene["Entities"]=[entities[i] for i in keep]
    scene["SkyVisible"]=False
    scene.pop("Environment",None)
    height=1.42 if case=="climb" else 0
    edge=.95 if case=="gap" else 0
    near=platform(1911,-1.5,0,[.3,.33,.37,1])
    far=platform(1912,edge+4,height,[.54,.48,.36,1])
    far["TransformComponent"]["Scale"][2]=8
    scene["Entities"].extend((near,far))

    camera=entities[1100]
    del camera["ScriptComponent"]
    camera["TransformComponent"]=transform((7.4,3.25,1.7),rotation=(-.27,math.pi/2,0))
    camera["CameraComponent"]["Camera"]["PerspectiveFov"]=.83
    entities[1101]["TransformComponent"]["Translation"]=[0,height+.07,4.7]
    entities[1102]["TransformComponent"]["Translation"]=[0,.07,-1.35]
    entities[1102]["TransformComponent"]["Rotation"]=[0,0,0]
    # 测试保留了瓦特的角色定义、领队编号和玩家脚本的 IK 设置。其他任务目标移到两座平台外。
    del entities[1103]["ScriptComponent"]
    for i in (1103,1110,1111,1112):
        entities[i]["TransformComponent"]["Translation"]=[15,.07,i-1103]
        entities[i].pop("ModelRendererComponent",None)


def verify(case,session,process,exe,output,hwnd):
    output.mkdir(parents=True,exist_ok=True)
    stream=session/"frames.jsonl"
    rows=[]
    offset=0

    def read():
        nonlocal offset
        if not stream.exists(): return rows
        with stream.open("rb") as file:
            file.seek(offset)
            for line in file:
                if not line.endswith(b"\n"): break
                offset+=len(line)
                frame=json.loads(line)
                actors={a["id"]:a for a in frame["actors"]}
                watt,leader=actors["1102"],actors["1101"]
                anim=watt.get("animation",{})
                nodes=anim.get("nodes",{})
                if "foot.L" not in nodes or "hand.L" not in nodes: continue
                rows.append(dict(frame=frame["seq"],seconds=frame["seconds"],action=watt["action"],
                                 stage=watt["stage"],clip=anim["clip"],time=anim["time"],watt=watt["position"],
                                 leader=leader["position"],foot=nodes["foot.L"],hand=nodes["hand.L"]))
        return rows

    stage="jump" if case=="gap" else "climb"
    clip="jump_1_up" if case=="gap" else "Station-Pickup"
    crossing=wait_for(lambda:next((r for r in read() if r["stage"]==stage and r["time"]>.24),None),process)
    save_view(hwnd,output/(case+".png"))
    wait_for(lambda:any(r["frame"]>crossing["frame"] and r["stage"]=="idle" for r in read()),process)
    settled=rows[-1]
    assert settled["watt"][2]>.95 and math.dist(settled["leader"],settled["watt"])<=2.01, settled
    assert abs(settled["watt"][1]-(1.49 if case=="climb" else .07))<.01

    # 相机从平台正侧面观察。按 A 让领队朝 +Z 再走一米，观察已经站定的瓦特是否也继续跟随。
    issue(session,1,"hold",key="A",lease_ms=4000)
    wait_for(lambda:read()[-1]["leader"][2]>settled["leader"][2]+1.,process)
    issue(session,1,"release",key="A")
    final=wait_for(lambda:next((r for r in read() if r["frame"]>settled["frame"] and r["stage"]=="idle"
                               and r["watt"][2]>settled["watt"][2]+.8),None),process)
    route=[r for r in rows if r["action"]=="follow" and r["frame"]<=final["frame"]]
    changes=[]
    for r in route:
        if not changes or changes[-1]["stage"]!=r["stage"]: changes.append(r)
    assert [r["stage"] for r in changes if r["stage"]!=""][:6]==["walk",stage,"walk","idle","walk","idle"], changes
    active=[r for r in route if r["stage"]==stage]
    assert all(r["clip"]==clip for r in active) and len(active)>=3
    assert max(r["watt"][1] for r in active)>.6
    assert math.dist(active[0]["hand"],active[-1]["hand"])>.15
    assert math.dist(active[0]["foot"],active[-1]["foot"])>.15
    assert all(r["clip"]=="walking_nogun-cycle" for r in route if r["stage"]=="walk")

    with (output/(case+".csv")).open("w",encoding="utf8",newline="") as file:
        writer=csv.writer(file,lineterminator="\n")
        writer.writerow(("frame","seconds","action","stage","clip","clip_s","watt_x","watt_y","watt_z",
                         "leader_x","leader_y","leader_z","left_foot_x","left_foot_y","left_foot_z",
                         "left_hand_x","left_hand_y","left_hand_z"))
        for r in route:
            writer.writerow((r["frame"],r["seconds"],r["action"],r["stage"],r["clip"],r["time"],
                             *r["watt"],*r["leader"],*r["foot"],*r["hand"]))
    result=dict(exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),scenario=case,role="assets/ai/watt.npc.yaml",
                start=route[0],peak_y_m=max(r["watt"][1] for r in active),settled=settled,final=final,changes=changes)
    (output/(case+".json")).write_text(json.dumps(result,indent=2,ensure_ascii=False)+"\n",encoding="utf8")
    print(case,[r["stage"] for r in changes],flush=True)
