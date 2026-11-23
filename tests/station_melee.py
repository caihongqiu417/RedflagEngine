"""由 check_watt_station.py --melee 调用。使用正式 C# 与模型，仅固定目标和障碍的位置。"""
import hashlib
import json
import math

from check_forge_play import wait_for
from playtest import issue


def arrange(scene, case):
    scene["SkyVisible"] = False
    scene.pop("Environment",None)
    scene["Entities"] = [row for row in scene["Entities"] if row["Entity"] in
                         (1100,1101,1102,1103,1110,1111,1112,1121,1900)]
    entities = {row["Entity"]:row for row in scene["Entities"]}
    guard = entities[1103]
    del guard["ScriptComponent"]
    del entities[1102]["NpcComponent"]
    guard["TransformComponent"]["Rotation"] = [0,0,0]
    # 朝 +X 的玩家站在 -6.9；巡检机的前表面与玩家相距约 0.63 米。
    guard["TransformComponent"]["Translation"] = [-2 if case in ("miss","movement") else -5.5,.08,1.4]
    if case=="movement":
        # 为落地后的三个完整步态周期留出直线距离，去掉岔路旁会挡住脚的补给箱。
        guard["TransformComponent"]["Translation"]=[6.5,.08,-3]
        entities[1112]["TransformComponent"]["Translation"]=[6,.08,-4]
    if case=="wall":
        scene["Entities"].append(dict(Entity=1910,TagComponent=dict(Tag="测试隔板"),
            TransformComponent=dict(Translation=[-6.47,1.27,1.4],Rotation=[0,0,0],Scale=[1,1,1]),
            RigidBody3DComponent=dict(BodyType="Static",Density=1),BoxCollider3DComponent=dict(HalfSize=[.03,.45,.45])))
    if case=="cancel":
        entities[1900]["ScriptComponent"]["ScriptFields"].append(dict(Name="InterruptPunch",Type="Bool",Value=True))


def verify(case, session, process, exe, output):
    stream = session/"frames.jsonl"

    def frames():
        if not stream.is_file(): return []
        return [json.loads(line) for line in stream.read_bytes().split(b"\n")[:-1]]

    def actor(frame, entity):
        return next(row for row in frame["actors"] if row["id"]==str(entity))

    def events(rows):
        return [(row,event) for row in rows for event in row["events"] if event["kind"].startswith("punch_")]

    wait_for(lambda: any(row["seq"]>=4 for row in frames()),process,70)
    if case=="movement":
        from station_landing_gait import verify as verify_gait
        verify_gait(session,process,frames,actor,output/"landing-walk.json",exe)
        check_movement(session, process, frames, actor, output, exe)
        return
    issue(session,1,"hold",key="F",lease_ms=10000)
    stop = "punch_cancelled" if case=="cancel" else "punch_end"
    if case=="miss":
        # 收拳前按住前进，同时尝试开火、跳跃和换装。它们不能盖住正在播的左拳。
        issue(session,1,"hold",key="W",lease_ms=10000)
        for key in ("MouseLeft","Space","2"):
            issue(session,1,"tap",key=key)
    wait_for(lambda: any(e["kind"]==stop for _,e in events(frames())),process,20)
    terminal = next(row for row,e in events(frames()) if e["kind"]==stop)
    wait_for(lambda: frames()[-1]["seconds"]>terminal["seconds"]+.35,process)
    rows = frames()
    issue(session,1,"release",key="F")
    if case=="miss": issue(session,1,"release",key="W")

    result = events(rows)
    kinds = [e["kind"] for _,e in result]
    contact = "punch_hit" if case=="hit" else "punch_miss"
    expected = ["punch_begin","punch_cancelled"] if case=="cancel" else ["punch_begin",contact,"punch_end"]
    assert kinds==expected, kinds
    if case!="cancel":
        first = {"hit":"1103","miss":"0","wall":"1910"}[case]
        assert result[1][1]["text"]==first, result[1]
    assert actor(rows[-1],1103)["health"]==(60 if case=="hit" else 75)
    if case=="cancel": assert actor(rows[-1],1101)["health"]==0
    start = result[0][0]
    during = [actor(r,1101) for r in rows if start["seq"]<=r["seq"]<terminal["seq"]]
    assert during and all(a["animation"]["clip"]=="Station-Punch" for a in during)
    x,z = during[0]["position"][::2]
    assert all(abs(a["position"][0]-x)<.001 and abs(a["position"][2]-z)<.001 for a in during)
    if case=="miss":
        assert actor(rows[-1],1101)["position"][0]>x+.2
        assert actor(rows[-1],1101)["animation"]["clip"]=="walking_gun-cycle"
    blocking = None
    if case=="wall":
        issue(session,1,"hold",key="W",lease_ms=4000)
        time=rows[-1]["seconds"]
        wait_for(lambda: frames()[-1]["seconds"]>time+1.2,process)
        stopped=frames()[-2:]
        issue(session,1,"release",key="W")
        blocking=[actor(row,1101) for row in stopped]
        assert all(a["animation"]["clip"]=="Idle-cycle" for a in blocking), blocking
        assert abs(blocking[0]["position"][0]-blocking[1]["position"][0])<.001
    report = dict(case=case,exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
                  events=[dict(frame=r["seq"],seconds=r["seconds"],kind=e["kind"],detail=e["text"]) for r,e in result],
                  holding_forward_at_wall=blocking,
                  sequences=[dict(frame=r["seq"],player=actor(r,1101),guard_health=actor(r,1103)["health"])
                             for r in rows if r["seq"]>=start["seq"]])
    output.mkdir(parents=True,exist_ok=True)
    path=output/(case+".json")
    path.write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n",encoding="utf8")
    print(path)


def check_movement(session, process, frames, actor, output, exe):
    # 相机先朝 +X，再绕过肩部。按每一次 W/A/S/D，实际位移要与镜头此时的前方或右方一致。
    def key(op,name):
        issue(session,1,op,key=name)

    def state():
        row=frames()[-1]
        return row,actor(row,1101)

    def advance(seconds):
        start=state()[0]["seconds"]
        wait_for(lambda:state()[0]["seconds"]>=start+seconds,process)

    issue(session,1,"move",x=310,y=170)
    key("hold","MouseRight")
    advance(.15)
    issue(session,1,"move",x=505,y=170)
    advance(.55)
    key("release","MouseRight")
    camera=actor(state()[0],1100)["position"]
    base=state()[1]["position"]
    assert abs(camera[2]-base[2])>1, (camera,base)

    results=[]
    for name in ("W","A","S","D"):
        first=state()[1]["position"]
        key("hold",name)
        advance(.32)
        end=state()[1]
        key("release",name)
        advance(.65)
        dx,dz=end["position"][0]-first[0],end["position"][2]-first[2]
        results.append(dict(key=name,dx=dx,dz=dz,clip=end["animation"]["clip"]))
        assert end["animation"]["clip"]=="walking_gun-cycle" and math.hypot(dx,dz)>.25
    # 在同一机位下，前后方向相反、左右方向相反，前进与横移近乎垂直。
    def cosine(i,j):
        a,b=results[i],results[j]
        return (a["dx"]*b["dx"]+a["dz"]*b["dz"])/(math.hypot(a["dx"],a["dz"])*math.hypot(b["dx"],b["dz"]))
    assert cosine(0,2)<-.95 and cosine(1,3)<-.95 and abs(cosine(0,1))<.15, results
    fx,fz=base[0]-camera[0],base[2]-camera[2]
    forward=results[0]
    assert (forward["dx"]*fx+forward["dz"]*fz)/(math.hypot(forward["dx"],forward["dz"])*math.hypot(fx,fz))>.95
    # 按住 Shift、Ctrl 与 W 分别播放跑步和蹲行，松开 W 后仍保持蹲姿。
    poses=[]
    for button,clip in (("Shift","running_gun-cycle"),("Ctrl","Station-CrouchWalk")):
        key("hold",button)
        key("hold","W")
        wait_for(lambda:state()[1]["animation"]["clip"]==clip,process)
        advance(.25)
        poses.append(state()[1]["animation"])
        key("release","W")
        if button=="Ctrl":
            wait_for(lambda:state()[1]["animation"]["clip"]=="Station-Crouch",process)
            poses.append(state()[1]["animation"])
        key("release",button)
        advance(.4)
    output.mkdir(parents=True,exist_ok=True)
    path=output/"movement.json"
    path.write_text(json.dumps(dict(exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),orbit_camera=camera,
                                     player=base,directions=results,poses=poses),indent=2)+"\n",encoding="utf8")
    print(path)
