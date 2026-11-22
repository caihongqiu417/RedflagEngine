"""Run qrenderdoc --python tests/replay_compute_memory.py from the repository root."""
import hashlib
import json
from pathlib import Path
from statistics import median
import sys
import traceback

import renderdoc as rd

OUT = Path("output/profiling/compute-memory")
CAPTURES = {"host": Path("output/profiling/stanford/StanfordBunny_capture.rdc"),
            "local": OUT / "StanfordBunnyLocal.rdc"}


def collect(actions):
    for action in actions:
        if action.flags & rd.ActionFlags.Dispatch:
            yield action
        yield from collect(action.children)


def replay(path):
    capture = rd.OpenCaptureFile()
    controller = None
    try:
        assert capture.OpenFile(str(path), "", None) == rd.ResultCode.Succeeded
        status, controller = capture.OpenCapture(rd.ReplayOptions(), None)
        assert status == rd.ResultCode.Succeeded
        events = list(collect(controller.GetRootActions()))
        assert len(events) == 12
        counter = rd.GPUCounter.EventGPUDuration
        assert counter in controller.EnumerateCounters()
        controller.FetchCounters([counter])
        samples = [{r.eventId: r.value.d * 1000 for r in controller.FetchCounters([counter])} for _ in range(7)]
        events_out = []
        names = ("probe-update", "ddgi", "gi-candidates", "restir-gi", "di-candidates", "restir-di",
                 "direct", "gtao", "ssr", "cull", "cluster", "taa")
        bindings = {"ddgi": 8, "restir-gi": 8, "restir-di": 8, "direct": 8, "gtao": 3, "ssr": 3,
                    "cluster": 4, "taa": 5}
        for event, name in zip(events, names):
            times = [row[event.eventId] for row in samples]
            item = dict(name=name, eid=event.eventId, groups=list(event.dispatchDimension),
                        gpu_ms=times, median_ms=median(times))
            if name in bindings:
                controller.SetFrameEvent(event.eventId, True)
                state = controller.GetPipelineState()
                shader = state.GetShaderReflection(rd.ShaderStage.Compute)
                resources = state.GetReadWriteResources(rd.ShaderStage.Compute)
                resource = next(r.descriptor.resource for r in resources
                                if shader.readWriteResources[r.access.index].fixedBindNumber == bindings[name])
                image = bytes(controller.GetBufferData(resource, 0, 288 * 192 * 16))
                assert len(image) == 288 * 192 * 16
                item["rgba32f_sha256"] = hashlib.sha256(image).hexdigest()
            events_out.append(item)
        return dict(path=path.as_posix(), sha256=hashlib.sha256(path.read_bytes()).hexdigest(), events=events_out,
                    messages=[m.description for m in controller.GetFrameInfo().debugMessages])
    finally:
        if controller:
            controller.Shutdown()
        capture.Shutdown()


report = dict(renderdoc=rd.GetVersionString())
try:
    report["captures"] = {key: replay(path) for key, path in CAPTURES.items()}
    before, after = (report["captures"][key]["events"] for key in ("host", "local"))
    assert all(a["groups"] == b["groups"] and a.get("rgba32f_sha256") == b.get("rgba32f_sha256")
               for a, b in zip(before, after)), "Compute results differ between memory types"
    report["result"] = "same geometry, event groups and eight complete RGBA32F images"
except BaseException:
    report["error"] = traceback.format_exc()
(OUT / "replay.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf8")
sys.exit(0)
