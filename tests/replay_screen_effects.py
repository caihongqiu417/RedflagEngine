"""Run from the repository root with qrenderdoc --python tests/replay_screen_effects.py."""
import hashlib
import json
import sys
import traceback
from pathlib import Path
from statistics import median

import renderdoc as rd

output = Path("output/profiling/screen-effects")
raw = Path("tmp/screen-effects")
raw.mkdir(parents=True, exist_ok=True)
rdc = output / "SponzaScreen_capture.rdc"
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
    assert len(actions) == 5
    report["renderdoc"] = rd.GetVersionString()
    report["sha256"] = hashlib.sha256(rdc.read_bytes()).hexdigest()
    report["messages"] = [m.description for m in controller.GetFrameInfo().debugMessages]
    report["dispatches"] = []

    duration = rd.GPUCounter.EventGPUDuration
    timings = []
    if duration in controller.EnumerateCounters():
        controller.FetchCounters([duration])
        for _ in range(9):
            timings.append({value.eventId: value.value.d*1000 for value in controller.FetchCounters([duration])})
    # 前三道读写像素，第四道保存灯光位集合，最后一道根据它照亮像素。
    outputs = [("gtao", 3), ("ssr", 3), ("taa", 5), ("cluster-mask", 3), ("clustered", 4)]
    for action, (name, binding) in zip(actions, outputs):
        controller.SetFrameEvent(action.eventId, True)
        state = controller.GetPipelineState()
        resources = state.GetReadWriteResources(rd.ShaderStage.Compute)
        reflection = state.GetShaderReflection(rd.ShaderStage.Compute)
        target = next(item.descriptor.resource for item in resources
                      if reflection.readWriteResources[item.access.index].fixedBindNumber == binding)
        count = 11 * 6 * 16 * 2 * 4 if name == "cluster-mask" else 161 * 91 * 16
        data = bytes(controller.GetBufferData(target, 0, count))
        assert len(data) == count
        (raw / ("replay-" + name + ".bin")).write_bytes(data)
        ms = [sample[action.eventId] for sample in timings]
        report["dispatches"].append(dict(name=name, eid=action.eventId, groups=list(action.dispatchDimension),
            binding=binding, resource=str(target), bytes=count,
            replay_gpu_samples_ms=ms, replay_gpu_median_ms=median(ms) if ms else None))
    report["result"] = "ok"
except BaseException:
    report["error"] = traceback.format_exc()
finally:
    if controller:
        controller.Shutdown()
    capture.Shutdown()
    (output / "renderdoc.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
sys.exit(0)
