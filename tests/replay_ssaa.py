"""由 profile_ssaa.py 在 RenderDoc 的 Python 解释器中运行。"""
import json
import os
from pathlib import Path
import struct
import sys
import traceback
from statistics import median

import renderdoc as rd


def walk(items):
    for item in items:
        yield item
        yield from walk(item.children)


def slots(frame):
    state=frame.GetPipelineState()
    shader=state.GetShaderReflection(rd.ShaderStage.Compute)
    return {shader.readWriteResources[r.access.index].fixedBindNumber:r.descriptor.resource
            for r in state.GetReadWriteResources(rd.ShaderStage.Compute)}


def read(frame,work):
    textures={r.resourceId:r for r in frame.GetTextures()}
    buffers={r.resourceId:r for r in frame.GetBuffers()}
    actions=list(walk(frame.GetRootActions()))
    draws=[a for a in actions if a.flags & rd.ActionFlags.Drawcall]
    dispatches=[a for a in actions if a.flags & rd.ActionFlags.Dispatch]
    last=dispatches[-1]
    frame.SetFrameEvent(last.eventId,True)
    layout=slots(frame)
    assert set(layout)=={0,1} and layout[0] in buffers and layout[1] in textures
    panel=textures[layout[1]]
    gray,side=struct.unpack_from("<II",bytes(frame.GetVulkanPipelineState().pushconsts))
    assert gray==0 and side in (1,2)
    w,h=panel.width,panel.height
    n=w*h*side*side
    assert buffers[layout[0]].length==16*n
    (work/"ssaa-panel.f32").write_bytes(frame.GetTextureData(panel.resourceId,rd.Subresource()))
    (work/"ssaa-light.f32").write_bytes(frame.GetBufferData(layout[0],0,16*n))

    bound_buffers=set()
    bound_textures=set()
    packed=None
    for a in dispatches:
        frame.SetFrameEvent(a.eventId,True)
        inputs=slots(frame)
        bound_buffers.update(v for v in inputs.values() if v in buffers)
        bound_textures.update(v for v in inputs.values() if v in textures)
        if len(inputs)==3 and inputs.get(0) in textures and inputs.get(1) in textures and inputs.get(2) in buffers:
            dims=[[textures[inputs[i]].width,textures[inputs[i]].height] for i in (0,1)]
            if dims==[[w*side,h*side]]*2:
                assert packed is None
                packed=dict(event=a.eventId,images=dims,groups=list(a.dispatchDimension),
                            image_bytes=[textures[inputs[i]].byteSize for i in (0,1)],surface_bytes=buffers[inputs[2]].length)
    assert packed is not None,"No full-size pack-surfaces dispatch in this frame"

    groups={}
    for draw in draws:
        images=[textures[t] for t in draw.outputs if t in textures]
        size="other" if not images else f"{images[0].width}x{images[0].height}"
        groups.setdefault(size,[]).append(draw.eventId)
    totals=dict(buffers=len(buffers),buffer_bytes=sum(b.length for b in buffers.values()),
                images=len(textures),image_bytes=sum(t.byteSize for t in textures.values()),
                compute_buffers=len(bound_buffers),compute_buffer_bytes=sum(buffers[b].length for b in bound_buffers),
                compute_images=len(bound_textures),compute_image_bytes=sum(textures[t].byteSize for t in bound_textures))
    info=frame.GetFrameInfo()
    return dict(frame=info.frameNumber,renderdoc=rd.GetVersionString(),api=frame.GetAPIProperties().localRenderer.name,
                messages=[m.description for m in info.debugMessages], draws=len(draws),dispatches=len(dispatches),
                draw_target_sizes={k:len(v) for k,v in groups.items()},draw_events=groups,
                storage=totals,pack=packed,panel=dict(event=last.eventId,size=[w,h],sample_side=side,groups=list(last.dispatchDimension),
                    source_size=[w*side,h*side],source_bytes=buffers[layout[0]].length,target_bytes=panel.byteSize,
                    source_id=str(layout[0]),target_id=str(layout[1])))


def gpu_times(out):
    result={}
    counter=rd.GPUCounter.EventGPUDuration
    for mode in ("off","on","on","off"):
        metadata=json.loads((out/(mode+"-capture.json")).read_bytes())
        assert metadata["executable_sha256"]==os.environ["RF_SSAA_EXE_SHA"]
        r=json.loads((out/(mode+"-replay.json")).read_bytes())
        cap=rd.OpenCaptureFile()
        frame=None
        try:
            assert cap.OpenFile(str(Path(metadata["frame"]).resolve()),"",None)==rd.ResultCode.Succeeded
            status,frame=cap.OpenCapture(rd.ReplayOptions(),None)
            assert status==rd.ResultCode.Succeeded and counter in frame.EnumerateCounters()
            calls=list(walk(frame.GetRootActions()))
            dispatches=[a.eventId for a in calls if a.flags & rd.ActionFlags.Dispatch]
            draws=[a.eventId for a in calls if a.flags & rd.ActionFlags.Drawcall]
            high="x".join(str(v) for v in r["panel"]["source_size"])
            groups=dict(scene_draws=r["draw_events"][high],compute=dispatches,
                        panel=[r["panel"]["event"]],draws_and_compute=draws+dispatches)
            if mode not in result:
                submits=[c.metadata.durationMicro for c in frame.GetStructuredFile().chunks if c.name=="vkQueueSubmit"]
                result[mode]=dict(groups={name:[] for name in groups},queue_submit_cpu_us=submits)
            frame.FetchCounters([counter])
            for _ in range(6):
                times={v.eventId:v.value.d*1000 for v in frame.FetchCounters([counter])}
                for name,events in groups.items():
                    result[mode]["groups"][name].append(sum(times[e] for e in events))
        finally:
            if frame: frame.Shutdown()
            cap.Shutdown()
    for item in result.values():
        item["median_ms"]={k:median(v) for k,v in item["groups"].items()}
    return dict(renderdoc=rd.GetVersionString(),executable_sha256=os.environ["RF_SSAA_EXE_SHA"],cases=result,
                scope="EventGPUDuration in milliseconds. Replays in off/on/on/off order; discard one per visit then record six. Sums contain named draw/dispatch events only; exclude clears, copies, idle intervals and presentation.")


if "RF_SSAA_TIMINGS" in os.environ:
    out=Path(os.environ["RF_SSAA_TIMINGS"])
    try: result=gpu_times(out)
    except Exception: result={"error":traceback.format_exc()}
    (out/"gpu.json").write_text(json.dumps(result,indent=2)+"\n",encoding="utf8")
    sys.exit(1 if "error" in result else 0)

work=Path(os.environ["RF_SSAA_WORK"])
capture=rd.OpenCaptureFile()
frame=None
try:
    assert capture.OpenFile(os.environ["RF_SSAA_CAPTURE"],"",None)==rd.ResultCode.Succeeded
    status,frame=capture.OpenCapture(rd.ReplayOptions(),None)
    assert status==rd.ResultCode.Succeeded
    result=read(frame,work)
except Exception:
    result=dict(error=traceback.format_exc())
finally:
    if frame: frame.Shutdown()
    capture.Shutdown()
    (work/"ssaa-replay.json").write_text(json.dumps(result,indent=2)+"\n",encoding="utf8")
sys.exit(1 if "error" in result else 0)
