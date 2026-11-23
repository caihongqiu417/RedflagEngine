"""验证落地这种单次动作切回走路后，双脚会继续交替，不会停在剪辑末尾。"""
import hashlib
import json
from check_forge_play import wait_for
from playtest import issue
from station_pose import visible_pose


def verify(session,process,frames,actor,output,exe):
    def player(row):
        return actor(row,1101)

    def clip(row):
        return visible_pose(player(row))["clip"]

    start=frames()[-1]
    issue(session,1,"tap",key="Space")
    land=wait_for(lambda:next((r for r in frames() if r["seq"]>start["seq"] and
                              clip(r)=="jump_5_hardlanding"),None),process)
    issue(session,1,"hold",key="W",lease_ms=15000)

    def cycles():
        rows=[r for r in frames() if r["seq"]>land["seq"] and clip(r)=="walking_gun-cycle"]
        times=[player(r)["animation"]["time"] for r in rows]
        wraps=[i for i in range(1,len(times)) if times[i]<times[i-1]]
        return (rows,wraps) if len(wraps)>=3 else None

    rows,wraps=wait_for(cycles,process,25)
    issue(session,1,"release",key="W")
    samples=[]
    for r in rows:
        p=player(r)
        left,right=(p["animation"]["nodes"][n] for n in ("foot.L","foot.R"))
        samples.append(dict(frame=r["seq"],time=p["animation"]["time"],x=p["position"][0],
                            left_x=left[0],right_x=right[0],left_y=left[1],right_y=right[1]))

    strides=[]
    for begin,end in zip(wraps,wraps[1:]):
        step=samples[begin:end]
        lead=[s["left_x"]-s["right_x"] for s in step]
        left=[s["left_y"] for s in step]
        right=[s["right_y"] for s in step]
        stride=dict(first=step[0]["frame"],last=step[-1]["frame"],
                    left_leads_m=max(lead),right_leads_m=-min(lead),
                    left_lift_m=max(left)-min(left),right_lift_m=max(right)-min(right))
        assert min(stride["left_leads_m"],stride["right_leads_m"])>.15, stride
        assert min(stride["left_lift_m"],stride["right_lift_m"])>.03, stride
        strides.append(stride)
    phases=list(dict.fromkeys(clip(r) for r in frames() if start["seq"]<=r["seq"]<=rows[-1]["seq"]))
    assert "jump_1_up" in phases and "jump_3_midair-cycle" in phases and "jump_5_hardlanding" in phases, phases
    result=dict(exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),scene="Station player on a clear test platform",
                clips=phases,landing_frame=land["seq"],wrap_frames=[samples[i]["frame"] for i in wraps],
                travel_m=samples[-1]["x"]-samples[0]["x"],strides=strides,frames=samples)
    output.parent.mkdir(parents=True,exist_ok=True)
    output.write_text(json.dumps(result,indent=2)+"\n",encoding="utf8")
    # 回到起跳处，下一组环视和 WASD 检查仍然从原来的空地开始。
    issue(session,1,"hold",key="S",lease_ms=15000)
    wait_for(lambda:player(frames()[-1])["position"][0]<=player(start)["position"][0],process)
    issue(session,1,"release",key="S")
