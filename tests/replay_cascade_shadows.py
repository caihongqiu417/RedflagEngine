"""由 capture_cascade_shadows.py 调起，在 RenderDoc 回放中读取 GPU 事件耗时。"""
import json
import os
from pathlib import Path
import statistics
import struct
import sys
import traceback

import renderdoc as rd


def actions(nodes):
    for node in nodes:
        yield node
        yield from actions(node.children)


def measure(frame):
    groups = {"draw": [], "store": [], "visibility": [], "direct": []}
    for action in actions(frame.GetRootActions()):
        event = action.eventId
        if action.flags & rd.ActionFlags.Drawcall:
            groups["draw"].append(event)
        elif action.flags & rd.ActionFlags.Dispatch:
            frame.SetFrameEvent(event, True)
            pipe = frame.GetPipelineState()
            shader = pipe.GetShaderReflection(rd.ShaderStage.Compute)
            sizes = [block.byteSize for block in shader.constantBlocks]
            slots = len(pipe.GetReadWriteResources(rd.ShaderStage.Compute))
            if sizes == [112] and slots == 2:
                groups["store"].append(event)
            elif sizes == [16] and slots == 3 and tuple(shader.dispatchThreadsDimension) == (64,1,1):
                groups["visibility"].append(event)
            elif sizes == [20] and struct.unpack_from("I",bytes(frame.GetVulkanPipelineState().pushconsts))[0] in (3,7):
                groups["direct"].append(event)

    if groups["store"]:
        assert len(groups["store"]) == 3 and len(groups["draw"]) == 9 and len(groups["visibility"]) == 1
    else:
        assert len(groups["direct"]) == 1
    values = {key: [] for key, ids in groups.items() if ids}
    counter = rd.GPUCounter.EventGPUDuration
    frame.FetchCounters([counter])
    for _ in range(11):
        times = {v.eventId: v.value.d*1000 for v in frame.FetchCounters([counter])}
        for key in values:
            values[key].append(sum(times[i] for i in groups[key]))
    return dict(tool=rd.GetVersionString(), events=groups, gpu_ms=values,
        median_ms={key: statistics.median(times) for key,times in values.items()})


result = Path(os.environ["RF_CASCADE_TIMINGS"])
file = rd.OpenCaptureFile()
frame = None
try:
    assert file.OpenFile(os.environ["RF_CASCADE_FRAME"], "", None) == rd.ResultCode.Succeeded
    status, frame = file.OpenCapture(rd.ReplayOptions(), None)
    assert status == rd.ResultCode.Succeeded
    result.write_text(json.dumps(measure(frame),indent=2)+"\n",encoding="utf8")
except Exception:
    result.write_text(traceback.format_exc(),encoding="utf8")
    sys.exit(1)
finally:
    if frame: frame.Shutdown()
    file.Shutdown()
sys.exit(0)
