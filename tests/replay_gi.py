"""Run with qrenderdoc --python tests/replay_gi.py from the repository root."""
import hashlib
import json
import sys
import traceback
from pathlib import Path
from statistics import median

import renderdoc as rd

raw = Path("tmp/gi-benchmark")
out = Path("output/profiling/gi")
raw.mkdir(parents=True, exist_ok=True)
rdc = out / "SponzaGI_capture.rdc"
capture = rd.OpenCaptureFile()
controller = None
report = {}


def dispatches(actions):
    for action in actions:
        if action.flags & rd.ActionFlags.Dispatch:
            yield action
        yield from dispatches(action.children)


try:
    assert capture.OpenFile(str(rdc), "", None) == rd.ResultCode.Succeeded
    status, controller = capture.OpenCapture(rd.ReplayOptions(), None)
    assert status == rd.ResultCode.Succeeded
    actions = list(dispatches(controller.GetRootActions()))
    assert len(actions) == 6
    report["renderdoc"] = rd.GetVersionString()
    report["sha256"] = hashlib.sha256(rdc.read_bytes()).hexdigest()
    report["messages"] = [message.description for message in controller.GetFrameInfo().debugMessages]
    report["dispatches"] = []
    duration = rd.GPUCounter.EventGPUDuration
    timings = []
    if duration in controller.EnumerateCounters():
        controller.FetchCounters([duration])
        for _ in range(9):
            timings.append({r.eventId: r.value.d*1000 for r in controller.FetchCounters([duration])})

    stages = ["probe", "ddgi", "di-candidates", "di", "gi-candidates", "gi"]
    for action, stage in zip(actions, stages):
        ms = [sample[action.eventId] for sample in timings]
        report["dispatches"].append(dict(name=stage, eid=action.eventId, groups=list(action.dispatchDimension),
            replay_gpu_samples_ms=ms, replay_gpu_median_ms=median(ms) if ms else None))
        if stage not in ("ddgi", "di", "gi"):
            continue
        controller.SetFrameEvent(action.eventId, True)
        state = controller.GetPipelineState()
        resources = state.GetReadWriteResources(rd.ShaderStage.Compute)
        reflection = state.GetShaderReflection(rd.ShaderStage.Compute)
        target = next(item.descriptor.resource for item in resources
                      if reflection.readWriteResources[item.access.index].fixedBindNumber == 8)
        data = bytes(controller.GetBufferData(target, 0, 160*90*16))
        assert len(data) == 160*90*16
        (raw / ("replay-" + stage + ".f32")).write_bytes(data)
        report[stage + "_output_resource"] = str(target)
    report["result"] = "ok"
except BaseException:
    report["error"] = traceback.format_exc()
finally:
    if controller:
        controller.Shutdown()
    capture.Shutdown()
    (out / "renderdoc.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
sys.exit(0)
