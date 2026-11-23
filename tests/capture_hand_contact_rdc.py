"""抓取第一次 H 自疗、手里出现医疗包的一帧。只在解压副本里摆相机和角色。"""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import time
import traceback
import zipfile

ROOT=Path(os.environ["RF_PROP_ROOT"]) if "RF_PROP_ROOT" in os.environ else Path(__file__).resolve().parents[1]


def record(work,out):
    import renderdoc as rd
    sys.path.insert(0,str(ROOT/"scripts/redflag_mcp"))
    from playtest import current,issue
    tools=Path(os.environ["RF_PROP_TOOLS"]);session=Path(os.environ["RF_PROP_SESSION"])
    env=dict(os.environ,REDFLAG_PLAYTEST=str(session),VK_ADD_LAYER_PATH=str(tools),VK_INSTANCE_LAYERS="VK_LAYER_RENDERDOC_Capture")
    command=[str(tools/"renderdoccmd.exe"),"capture","-d",str(work),"-c",str(out/"hand"),
             str(work/"RedflagForge.exe"),"--vulkan","--play","assets/scenes/Watt-Station.redflag","--render-size","1280x720"]
    with (out/"launcher.log").open("wb") as log:
        launcher=subprocess.run(command,env=env,stdout=log,stderr=log,timeout=50)
    found=re.search(rb"\bID\s+(\d+)",(out/'launcher.log').read_bytes())
    target=rd.CreateTargetControl("localhost",int(found.group(1)) if found else launcher.returncode,"Station hand",True)
    try:
        assert target and target.Connected()
        tap=False;requested=None;path=None;deadline=time.monotonic()+125
        while target.Connected() and time.monotonic()<deadline:
            f=current(session).get("frame")
            if f:
                if not tap and f["seq"]>=7:issue(session,1,"tap","H");tap=True
                a=next(a for a in f["actors"] if a["id"]=="1101")
                c=a.get("contact")
                if requested is None and c and c.get("prop","0")!="0":
                    target.TriggerCapture(1);requested=f["seq"]
            message=target.ReceiveMessage(None)
            if message.type==rd.TargetControlMessageType.NewCapture:
                path=message.newCapture.path;break
            time.sleep(.008)
        assert path,"Record did not receive a held item"
    finally:
        issue(session,1,"end")
        if target:target.Shutdown()
    file=rd.OpenCaptureFile();assert file.OpenFile(path,"",None)==rd.ResultCode.Succeeded
    status,ctrl=file.OpenCapture(rd.ReplayOptions(),None);assert status==rd.ResultCode.Succeeded
    def walk(nodes):
        for n in nodes:yield n;yield from walk(n.children)
    try:
        acts=list(walk(ctrl.GetRootActions())); draws=[a for a in acts if a.flags&rd.ActionFlags.Drawcall]
        tex={t.resourceId:t for t in ctrl.GetTextures()}
        groups=[]
        for pos,a in enumerate(draws[:-2]):
            parts=draws[pos:pos+3]
            if [d.numIndices for d in parts]!=[1182,840,240]:continue
            ctrl.SetFrameEvent(a.eventId,True)
            colors=ctrl.GetPipelineState().GetOutputTargets()
            dims=[[tex[o.resource].width,tex[o.resource].height] for o in colors if o.resource in tex]
            depth=ctrl.GetPipelineState().GetDepthTarget().resource
            groups.append(dict(draws=[d.eventId for d in parts],attachments=dims,depth=[tex[depth].width,tex[depth].height] if depth in tex else None))
        counters=rd.GPUCounter.EventGPUDuration;ctrl.FetchCounters([counters])
        measured=[]
        for _ in range(5):
            samples={d.eventId:d.value.d*1000 for d in ctrl.FetchCounters([counters])}
            measured.append(dict(draw_dispatch_ms=sum(samples.get(a.eventId,0) for a in acts if a.flags&(rd.ActionFlags.Drawcall|rd.ActionFlags.Dispatch)),
                                 medkit_ms=[sum(samples.get(e,0) for e in g["draws"]) for g in groups]))
        facts=dict(capture=path,requested_seq=requested,frame=ctrl.GetFrameInfo().frameNumber,
                   draws=len(draws),dispatches=len([a for a in acts if a.flags&rd.ActionFlags.Dispatch]),model_index_counts=[1182,840,240],model_passes=groups,
                   gpu_replays=measured,messages=[m.description for m in ctrl.GetDebugMessages() if m.severity<=rd.MessageSeverity.Medium])
    finally:ctrl.Shutdown();file.Shutdown()
    facts["rdc_sha256"]=hashlib.sha256(Path(path).read_bytes()).hexdigest()
    (out/"replay.json").write_text(json.dumps(facts,indent=2)+"\n",encoding="utf8")


def main():
    from capture_material_studio import run_renderdoc
    from capture_station_hands import stage
    from capture_player_motion import NODES
    from playtest import begin,write_json
    import yaml
    p=argparse.ArgumentParser(description=__doc__);p.add_argument("archive",type=Path);p.add_argument("output",type=Path)
    p.add_argument("--renderdoc",type=Path,default=ROOT/"tmp/tools/renderdoc/RenderDoc_1.47_64")
    args=p.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="station-rdc-") as tmp:
        work=Path(tmp)
        with zipfile.ZipFile(args.archive) as z:z.extractall(work)
        scene=work/"assets/scenes/Watt-Station.redflag";s=yaml.safe_load(scene.read_bytes())
        stage(s,"player","heal","yard")
        # 只保留手中一个同模型物件，能按 GLB 的三个索引数连续找到药包的每次绘制。
        next(e for e in s["Entities"] if e["Entity"]==1112).pop("ModelRendererComponent")
        scene.write_text(yaml.safe_dump(s,allow_unicode=True,sort_keys=False),encoding="utf8")
        info=begin(work,str(scene),"GPU 手持医疗包",launch=False);session=work/"captures/playtests"/info["session"]
        info["pose_nodes"]=NODES;write_json(session/"session.json",info)
        env=dict(os.environ,RF_PROP_ROOT=str(ROOT),RF_PROP_WORK=str(work),RF_PROP_OUT=str(out),RF_PROP_SESSION=str(session),RF_PROP_TOOLS=str(args.renderdoc.resolve()))
        run_renderdoc(args.renderdoc.resolve(),Path(__file__).resolve(),ROOT,env)
        (out/"frames.jsonl.gz").write_bytes(gzip.compress((session/"frames.jsonl").read_bytes(),mtime=0))
        result=json.loads((out/"replay.json").read_bytes())
        for k,f in (("zip_sha256",args.archive),("exe_sha256",work/"RedflagForge.exe")):result[k]=hashlib.sha256(f.read_bytes()).hexdigest()
        (out/"replay.json").write_text(json.dumps(result,indent=2)+"\n",encoding="utf8")


if "RF_PROP_WORK" in os.environ:
    try:
        Path(os.environ["RF_PROP_OUT"],"capture.log").write_text("waiting for game\n",encoding="utf8")
        record(Path(os.environ["RF_PROP_WORK"]),Path(os.environ["RF_PROP_OUT"]))
    except Exception:
        (Path(os.environ["RF_PROP_OUT"])/"error.txt").write_text(traceback.format_exc(),encoding="utf8");sys.exit(1)
    sys.exit(0)
elif __name__=="__main__":main()
