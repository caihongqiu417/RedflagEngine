"""从仓库根目录运行 qrenderdoc --python tests/replay_point_materials.py。"""
import hashlib
import json
import struct
import sys
from statistics import median
from pathlib import Path

import renderdoc as rd

folder = Path("output/profiling/point-materials")
folder.mkdir(parents=True, exist_ok=True)
file = Path("output/captures/local/PointMaterials_capture.rdc")
cap = rd.OpenCaptureFile()
assert cap.OpenFile(str(file), "", None) == rd.ResultCode.Succeeded
status, replay = cap.OpenCapture(rd.ReplayOptions(), None)
assert status == rd.ResultCode.Succeeded

def dispatches(actions):
    for action in actions:
        if action.flags & rd.ActionFlags.Dispatch:
            yield action
        yield from dispatches(action.children)

try:
    actions = list(dispatches(replay.GetRootActions()))
    assert len(actions) == 3
    report = {"tool": rd.GetVersionString(), "capture": file.as_posix(), "sha256": hashlib.sha256(file.read_bytes()).hexdigest(),
              "messages": [m.description for m in replay.GetFrameInfo().debugMessages], "stages": []}
    outputs = []
    counter = rd.GPUCounter.EventGPUDuration
    replay.FetchCounters([counter])
    timings = [{r.eventId:r.value.d*1e6 for r in replay.FetchCounters([counter])} for _ in range(11)]
    for action, name, binding in zip(actions, ("all lights", "cull", "clustered"), (8, None, 4)):
        stage = {"name": name, "event": action.eventId, "groups": list(action.dispatchDimension)}
        stage["gpu_us"] = [t[action.eventId] for t in timings]
        stage["median_gpu_us"] = median(stage["gpu_us"])
        report["stages"].append(stage)
        if binding is None:
            continue
        replay.SetFrameEvent(action.eventId, True)
        pipeline = replay.GetPipelineState()
        shader = pipeline.GetShaderReflection(rd.ShaderStage.Compute)
        buffer = next(v.descriptor.resource for v in pipeline.GetReadWriteResources(rd.ShaderStage.Compute)
                      if shader.readWriteResources[v.access.index].fixedBindNumber == binding)
        data = replay.GetBufferData(buffer, 0, 33 * 33 * 16)
        image = list(struct.iter_unpack("<4f", data))
        stage["center_rgb"] = image[len(image)//2][:3]
        outputs.append(image)
    error = max(abs(a[c] - b[c]) for a,b in zip(*outputs) for c in range(3))
    report["max_rgb_difference"] = error
    assert error < .002 and not report["messages"]
    (folder/"renderdoc.json").write_text(json.dumps(report,indent=2)+"\n",encoding="utf8")
finally:
    replay.Shutdown()
    cap.Shutdown()
sys.exit(0)
