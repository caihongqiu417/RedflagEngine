"""由 check_animated_shadows.py 交给 RenderDoc 的 Python 环境运行。"""
import json
import os
from pathlib import Path
import statistics
import struct
import sys
import traceback
import renderdoc as rd


def walk(actions):
    for action in actions:
        yield action
        yield from walk(action.children)


def export(frame, work):
    actions = list(walk(frame.GetRootActions()))
    draws = [a.eventId for a in actions if a.flags & rd.ActionFlags.Drawcall]
    dispatches = [a.eventId for a in actions if a.flags & rd.ActionFlags.Dispatch]
    tex = {r.resourceId for r in frame.GetTextures()}
    stages = {}
    for event in dispatches:
        frame.SetFrameEvent(event,True)
        pipe=frame.GetPipelineState()
        shader=pipe.GetShaderReflection(rd.ShaderStage.Compute)
        slots={shader.readWriteResources[r.access.index].fixedBindNumber:r.descriptor.resource
               for r in pipe.GetReadWriteResources(rd.ShaderStage.Compute)}
        push=[b.byteSize for b in shader.constantBlocks]
        opts=bytes(frame.GetVulkanPipelineState().pushconsts)
        groups=tuple(shader.dispatchThreadsDimension)
        if groups==(8,8,1) and len(slots)==3 and push==[64] and slots[0] in tex:
            work.joinpath("receivers.f32").write_bytes(frame.GetBufferData(slots[2],0,0))
            stages["camera_surfaces"]=event
        if groups==(8,8,1) and len(slots)==2 and slots[0] in tex and slots[1] not in tex:
            stages["store_depth"]=event
            prior=[i for i in dispatches if i<event]
            stages["caster_draws"]=[i for i in draws if prior[-1]<i<event]
        if groups==(64,1,1) and push==[20] and struct.unpack_from("I",opts)[0] in (3,7):
            stages["direct"]=event
            work.joinpath("direct.f32").write_bytes(frame.GetBufferData(slots[8],0,0))
        if "direct" not in stages and groups==(8,8,1) and 4 in slots and not push and set(slots.values()).isdisjoint(tex) and len(slots) in (7,10,11):
            stages["direct"]=event
            work.joinpath("direct.f32").write_bytes(frame.GetBufferData(slots[4],0,0))
    assert "direct" in stages and "camera_surfaces" in stages, stages
    result=dict(renderdoc=rd.GetVersionString(), frame=frame.GetFrameInfo().frameNumber,stages=stages,
                draws=len(draws),dispatches=len(dispatches),messages=[m.description for m in frame.GetFrameInfo().debugMessages])
    if os.environ["RF_SHADOW_TIMING"]=="1":
        groups=dict(all_draw_and_compute=draws+dispatches, direct=[stages["direct"]])
        if "store_depth" in stages: groups.update(caster_draws=stages["caster_draws"], store_depth=[stages["store_depth"]])
        timings={key:[] for key in groups}
        counter=rd.GPUCounter.EventGPUDuration
        frame.FetchCounters([counter])
        for _ in range(11):
            values={v.eventId:v.value.d*1000 for v in frame.FetchCounters([counter])}
            for key,events in groups.items(): timings[key].append(sum(values[i] for i in events))
        result.update(gpu_ms=timings,median_ms={k:statistics.median(v) for k,v in timings.items()})
    return result

work=Path(os.environ["RF_SHADOW_DIR"])
capture=rd.OpenCaptureFile()
controller=None
try:
    assert capture.OpenFile(os.environ["RF_SHADOW_CAP"],"",None)==rd.ResultCode.Succeeded
    status,controller=capture.OpenCapture(rd.ReplayOptions(),None)
    assert status==rd.ResultCode.Succeeded
    info=export(controller,work)
except Exception:
    info=dict(error=traceback.format_exc())
finally:
    if controller: controller.Shutdown()
    capture.Shutdown()
(work/"shadow.json").write_text(json.dumps(info,indent=2),encoding="utf8")
sys.exit(int("error" in info))
