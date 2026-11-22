"""由 check_game_hud.py 使用 RenderDoc 的 Python 回放接口运行。"""
import json
import os
from pathlib import Path
import struct
import sys
import traceback
import renderdoc as rd


def draws(actions):
    for a in actions:
        if a.flags & rd.ActionFlags.Drawcall:
            yield a
        yield from draws(a.children)


def replay(frame, out):
    textures = {t.resourceId:t for t in frame.GetTextures()}
    actions = list(draws(frame.GetRootActions()))
    frame.SetFrameEvent(actions[-1].eventId, True)
    target = frame.GetPipelineState().GetOutputTargets()[0].resource
    size = [textures[target].width, textures[target].height]
    save = rd.TextureSave()
    save.resourceId = target
    save.destType = rd.FileType.PNG
    assert frame.SaveTexture(save, str(out / "game.png")) == rd.ResultCode.Succeeded
    # 游戏和编辑器的其他图像此前已在别的目标上画好。这里只检查最后一张交换链图像的 ImGui 绘制。
    events = []
    for a in reversed(actions):
        frame.SetFrameEvent(a.eventId, True)
        state = frame.GetPipelineState()
        images = state.GetOutputTargets()
        if not images or images[0].resource != target: break
        vertex = state.GetVBuffers()[0]
        index = state.GetIBuffer()
        tex_id = state.GetReadOnlyResources(rd.ShaderStage.Fragment)[0].descriptor.resource
        tex = textures[tex_id]
        tex_file = str(tex_id).split(":")[-1] + "-alpha.bin"
        if not (out / tex_file).is_file():
            data = bytes(frame.GetTextureData(tex_id, rd.Subresource()))
            if len(data) == tex.width * tex.height * 4:
                (out / tex_file).write_bytes(data[3::4])
        scissor = state.GetScissor(0)
        raw = frame.GetBufferData(index.resourceId, index.byteOffset + a.indexOffset * index.byteStride, a.numIndices * index.byteStride)
        indices = struct.unpack("<" + ("H" if index.byteStride == 2 else "I") * a.numIndices, raw)
        first, last = min(indices) + a.baseVertex, max(indices) + a.baseVertex
        vertices = frame.GetBufferData(vertex.resourceId, vertex.byteOffset + first * vertex.byteStride, (last - first + 1) * vertex.byteStride)
        (out / f"vertices-{a.eventId}.bin").write_bytes(vertices)
        (out / f"indices-{a.eventId}.bin").write_bytes(raw)
        events.append(dict(event=a.eventId, texture=str(tex_id), texture_size=[tex.width,tex.height], indices=a.numIndices,
                           first_index=min(indices), stride=vertex.byteStride, index_size=index.byteStride, alpha=tex_file,
                           scissor=[scissor.x,scissor.y,scissor.width,scissor.height], push=bytes(frame.GetVulkanPipelineState().pushconsts)[:16].hex()))
    events.reverse()
    wanted = [v for v in (rd.GPUCounter.SamplesPassed,rd.GPUCounter.PSInvocations) if v in frame.EnumerateCounters()]
    counters = {(v.eventId,v.counter):v.value.u64 for v in frame.FetchCounters(wanted)}
    for rec in events:
        for k in wanted:
            if (rec["event"],k) in counters: rec[frame.DescribeCounter(k).name] = counters[rec["event"],k]
    if os.environ.get("RF_HUD_TIMING") == "1":
        duration = rd.GPUCounter.EventGPUDuration
        frame.FetchCounters([duration])
        for _ in range(11):
            values = {v.eventId:v.value.d * 1000 for v in frame.FetchCounters([duration])}
            for rec in events: rec.setdefault("duration_ms",[]).append(values[rec["event"]])
    return dict(renderdoc=rd.GetVersionString(),size=size,frame=frame.GetFrameInfo().frameNumber,events=events,
                messages=[m.description for m in frame.GetFrameInfo().debugMessages])


work = Path(os.environ["RF_HUD_WORK"])
capture = rd.OpenCaptureFile()
frame = None
try:
    assert capture.OpenFile(os.environ["RF_HUD_CAPTURE"], "", None) == rd.ResultCode.Succeeded
    result, frame = capture.OpenCapture(rd.ReplayOptions(),None)
    assert result == rd.ResultCode.Succeeded
    info = replay(frame,work)
except Exception:
    info = dict(error=traceback.format_exc())
finally:
    if frame: frame.Shutdown()
    capture.Shutdown()
(work / "hud.json").write_text(json.dumps(info,indent=2), encoding="utf8")
sys.exit(int("error" in info))
