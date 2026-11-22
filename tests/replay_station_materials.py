"""The capture runner invokes this file inside qrenderdoc."""
from array import array
import json
import math
import os
from pathlib import Path
import struct
import sys
import traceback

import renderdoc as rd


def actions(nodes):
    for node in nodes:
        yield node
        yield from actions(node.children)


def run(frame):
    events = list(actions(frame.GetRootActions()))
    draws = [a for a in events if a.flags & rd.ActionFlags.Drawcall]
    dispatches = [a.eventId for a in events if a.flags & rd.ActionFlags.Dispatch]
    frame.SetFrameEvent(draws[-1].eventId, True)
    target = frame.GetPipelineState().GetOutputTargets()[0].resource
    textures = {t.resourceId:t for t in frame.GetTextures()}
    save = rd.TextureSave()
    save.resourceId, save.destType = target, rd.FileType.PNG
    assert frame.SaveTexture(save,str(out/(label+".png"))) == rd.ResultCode.Succeeded

    selection = []
    seen = []
    view = label.split("-")[0]
    expected = {"rack":19116, "rim":49320}.get(view,6)
    for a in draws:
        if a.numIndices != expected: continue
        frame.SetFrameEvent(a.eventId, True)
        state = frame.GetPipelineState()
        targets = state.GetOutputTargets()
        if not targets or targets[0].resource not in textures: continue
        fmt = textures[targets[0].resource].format
        push = bytes(frame.GetVulkanPipelineState().pushconsts)
        if len(push)<128: continue
        # 路面在游戏中放大100倍，逆转置后的横、纵轴长度均为0.01。工具台的索引数由作者模型给出。
        normals = struct.unpack_from("<9f",push,84)
        seen.append((a.eventId,len(targets),fmt.Name(),normals))
        if len(targets)<2 or fmt.compType != rd.CompType.Float: continue
        if view in ("rack", "rim"):
            matrix = struct.unpack_from("<16f",push)
            yaw = 1.45 if view=="rack" else -.4
            if abs(normals[0]-math.cos(yaw))>.001 or abs(normals[2]+math.sin(yaw))>.001: continue
        elif any(abs(math.sqrt(sum(v*v for v in normals[i:i+3]))-.01)>1e-4 for i in (0,3)): continue
        else:
            matrix = struct.unpack_from("<16f",push)
        shader = state.GetShaderReflection(rd.ShaderStage.Fragment)
        reads = {shader.readOnlyResources[r.access.index].fixedBindSetOrSpace:
                 [textures[r.descriptor.resource].width,textures[r.descriptor.resource].height]
                 for r in state.GetReadOnlyResources(rd.ShaderStage.Fragment) if r.descriptor.resource in textures}
        flags = struct.unpack_from("<I",push,124)[0]
        selection.append(dict(event=a.eventId,mode=(flags>>7)&3,indices=a.numIndices,reads=reads,
                              clip_matrix=list(matrix)))
        normal_image, albedo = targets[1].resource, targets[0].resource
    if not selection:
        (work/"unmatched.json").write_text(json.dumps(seen,indent=2))
        raise ValueError("Could not identify the target material draw among "+str(len(draws))+" draws")
    assert len(selection) == 1, "Multiple draws match the chosen material"
    event = selection[0]["event"]
    if view == "rim":
        # 顶点阶段的第一个输出是裁剪空间位置。保留它，分开量轮毂几何和前景角色的移动。
        frame.SetFrameEvent(event, True)
        mesh = frame.GetPostVSData(0,0,rd.MeshDataStage.VSOut)
        stream = bytes(frame.GetBufferData(mesh.vertexResourceId,mesh.vertexByteOffset,0))
        assert mesh.vertexByteStride>=16 and len(stream)>=mesh.vertexByteStride
        with (work/"clip.f32").open("wb") as positions:
            for offset in range(0,len(stream)-mesh.vertexByteStride+1,mesh.vertexByteStride):
                positions.write(stream[offset:offset+16])
    for stage, number in (("before",event-1),("after",event),("final",draws[-1].eventId)):
        frame.SetFrameEvent(number,True)
        data = array("f")
        data.frombytes(bytes(frame.GetTextureData(normal_image,rd.Subresource())))
        (work/(stage+"-depth.f32")).write_bytes(data[3::4].tobytes())
        if stage=="final":
            (work/"normal.f32").write_bytes(data.tobytes())
            (work/"albedo.f32").write_bytes(frame.GetTextureData(albedo,rd.Subresource()))
    result = dict(renderdoc=rd.GetVersionString(),frame=frame.GetFrameInfo().frameNumber,
                  size=[textures[target].width,textures[target].height],material_draws=selection,
                  draws=len(draws),dispatches=len(dispatches),capture=str(file.relative_to(Path.cwd())),
                  messages=[m.description for m in frame.GetDebugMessages()
                            if m.severity in (rd.MessageSeverity.High,rd.MessageSeverity.Medium)])
    if os.environ["RF_RELIEF_TIME"] == "1":
        counter = rd.GPUCounter.EventGPUDuration
        frame.FetchCounters([counter])
        whole = {a.eventId for a in draws} | set(dispatches)
        for i in range(7):
            times = {s.eventId:s.value.d*1e6 for s in frame.FetchCounters([counter])}
            for s in selection: s.setdefault("gpu_us",[]).append(times[s["event"]])
            result.setdefault("draw_dispatch_us",[]).append(sum(times[e] for e in whole))
    return result


work, out = Path(os.environ["RF_RELIEF_WORK"]), Path(os.environ["RF_RELIEF_OUT"])
file, label = Path(os.environ["RF_RELIEF_RDC"]), os.environ["RF_RELIEF_LABEL"]
cap = rd.OpenCaptureFile()
ctrl = None
try:
    assert cap.OpenFile(str(file),"",None) == rd.ResultCode.Succeeded
    status, ctrl = cap.OpenCapture(rd.ReplayOptions(),None)
    assert status == rd.ResultCode.Succeeded
    report = run(ctrl)
except Exception:
    report = {"error":traceback.format_exc()}
finally:
    if ctrl: ctrl.Shutdown()
    cap.Shutdown()
(out/(label+".json")).write_text(json.dumps(report,indent=2)+"\n",encoding="utf8")
sys.exit(int("error" in report))
