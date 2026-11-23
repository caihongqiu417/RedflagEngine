"""让同一个第三人称镜头分别遇到低箱、高墙和顶棚，读取相机的实际世界位置。"""
import argparse
import copy
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile

import win32con
import win32event
import win32process
import win32service
import yaml

from capture_player_motion import digest


def measure(project):
    scene_file=project/"assets/scenes/Watt-Station.redflag"
    scene=yaml.safe_load(scene_file.read_bytes())
    camera=next(e for e in scene["Entities"] if e["Entity"]==1100)
    scene={"Scene":"肩后镜头的障碍测试","Entities":[]}
    cases=("open","crate","wall","ceiling","side_wall","lift_disabled")
    walls={}
    for i,name in enumerate(cases):
        z=i*15.
        def box(uid,tag,position,extent):
            scene["Entities"].append(dict(Entity=uid,TagComponent={"Tag":tag},
                TransformComponent=dict(Translation=position,Rotation=[0,0,0],Scale=[1,1,1]),
                RigidBody3DComponent={"BodyType":"Static","Density":1},BoxCollider3DComponent={"HalfSize":extent}))
        actor=5000+i
        box(actor,name+" target",[0,0,z],[.35,.92,.35])
        cam=copy.deepcopy(camera);cam["Entity"]=6000+i
        cam["TransformComponent"].update(Translation=[-4.7,1.38,z+.62],Rotation=[0,-math.pi/2,0])
        cam["CameraComponent"]["Primary"]=i==0
        fields=cam["ScriptComponent"]["ScriptFields"];fields[0]["Value"]=actor
        if name=="lift_disabled":fields.append({"Name":"ObstacleLift","Type":"Float","Value":0})
        scene["Entities"].append(cam)
        solids=[]
        if name in ("crate","ceiling","lift_disabled"):
            solids.append(([-1.1,.67,z+.62],[.30,.67,.52]))
        if name=="wall":solids.append(([-1.1,2.,z],[.30,2.,2.]))
        if name=="side_wall":solids.append(([-2.,1.5,z+.59],[6.,1.5,.05]))
        if name=="ceiling":solids.append(([-1.3,1.72,z],[3.0,.05,2.0]))
        walls[name]=solids
        for j,(center,half) in enumerate(solids):box(7000+i*10+j,name+str(j),center,half)

    test=project/"assets/scenes/Camera-obstacles.redflag"
    test.write_text(yaml.safe_dump(scene,allow_unicode=True,sort_keys=False),encoding="utf8")
    Path(str(test)+".render.yaml").write_text("Shading: Base color\n",encoding="utf8")
    session=project/"camera-session";session.mkdir()
    args=[str(project/"RedflagForge.exe"),"--vulkan","--play",str(test),"--render-size","1280x720","--frames","500"]
    desktop_name=f"CameraFixture{os.getpid()}"
    desktop=win32service.CreateDesktop(desktop_name,0,win32con.GENERIC_ALL,None)
    startup=win32process.STARTUPINFO();startup.lpDesktop="WinSta0\\"+desktop_name
    proc,thread,_,_=win32process.CreateProcess(None,subprocess.list2cmdline(args),None,None,False,
        win32con.CREATE_NO_WINDOW,dict(os.environ,REDFLAG_PLAYTEST=str(session)),str(project),startup)
    thread.Close()
    try:
        assert win32event.WaitForSingleObject(proc,60000)==win32con.WAIT_OBJECT_0
        assert win32process.GetExitCodeProcess(proc)==0
    finally:
        if win32process.GetExitCodeProcess(proc)==259:win32process.TerminateProcess(proc,1)
        proc.Close();desktop.CloseDesktop()
    rows=[json.loads(line) for line in (session/"frames.jsonl").read_bytes().splitlines()]
    result={}
    for i,name in enumerate(cases):
        cameras=[next(a["position"] for a in r["actors"] if a["id"]==str(6000+i)) for r in rows if r["seq"]>=3]
        eye=cameras[-1]
        distances=[math.sqrt(sum(max(abs(v-c)-h,0.)**2 for v,c,h in zip(p,center,half)))
                   for p in cameras for center,half in walls[name]]
        result[name]=dict(x_metres=eye[0],y_metres=eye[1],shoulder_metres=eye[2]-i*15.,
                          closest_surface_metres=min(distances) if distances else None)
        if distances:assert min(distances)>=.195,(name,min(distances))
    assert result["open"]["x_metres"]<-4.65 and abs(result["open"]["y_metres"]-1.38)<.02
    assert result["crate"]["x_metres"]<-4.65 and result["crate"]["y_metres"]>1.68,result
    for name in ("wall","ceiling","lift_disabled"):assert -.65<result[name]["x_metres"]<-.48,(name,result[name])
    assert abs(result["wall"]["y_metres"]-1.38)<.02
    assert result["side_wall"]["shoulder_metres"]<.35 and result["side_wall"]["x_metres"]<-4.65
    return result


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive",type=Path)
    parser.add_argument("output",type=Path)
    parser.add_argument("--sandbox",type=Path,help="用于只替换脚本的中间构建对照")
    args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="redflag-camera-") as folder:
        project=Path(folder)
        with zipfile.ZipFile(args.archive) as z:z.extractall(project)
        if args.sandbox:shutil.copyfile(args.sandbox,project/"Resources/Scripts/Sandbox.dll")
        data=dict(archive_sha256=digest(args.archive),sandbox_sha256=digest(project/"Resources/Scripts/Sandbox.dll"),
                  replaced_sandbox=bool(args.sandbox),cases=measure(project))
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(data,indent=2)+"\n",encoding="utf8")
    print(data)
