"""Run with qrenderdoc --python tests/replay_probe_feedback.py from the repository root."""
import hashlib
import json
from pathlib import Path
from statistics import median
import struct
import sys
import renderdoc as rd


def actions(nodes):
    for node in nodes:
        yield node
        yield from actions(node.children)


def replay(path, feedback):
    capture = rd.OpenCaptureFile()
    assert capture.OpenFile(str(path), "", None) == rd.ResultCode.Succeeded
    status, controller = capture.OpenCapture(rd.ReplayOptions(), None)
    assert status == rd.ResultCode.Succeeded
    try:
        events = [a for a in actions(controller.GetRootActions()) if a.flags & rd.ActionFlags.Dispatch]
        assert len(events) == 2
        controller.SetFrameEvent(events[0].eventId, True)
        stage, frame, samples, flags, seed = struct.unpack("<5I", bytes(controller.GetVulkanPipelineState().pushconsts)[:20])
        assert (stage, frame, samples, flags, seed) == (9, 3, 64, 2 | int(feedback), 137)
        counter = rd.GPUCounter.EventGPUDuration
        assert counter in controller.EnumerateCounters()
        controller.FetchCounters([counter])
        times = {e.eventId: [] for e in events}
        for _ in range(11):
            for sample in controller.FetchCounters([counter]):
                if sample.eventId in times:
                    times[sample.eventId].append(sample.value.d * 1000)
        return dict(file=path.as_posix(), sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                    tool=rd.GetVersionString(), ray_count=12*6*7*6*samples, stage=stage, frame=frame,
                    samples=samples, flags=flags, seed=seed,
                    dispatch=[dict(event=e.eventId, name=n, groups=list(e.dispatchDimension),
                                   ms=times[e.eventId], median_ms=median(times[e.eventId])) for e,n in zip(events,("update","gather"))],
                    messages=[m.description for m in controller.GetFrameInfo().debugMessages])
    finally:
        controller.Shutdown()
        capture.Shutdown()


output = Path("output/profiling/probe-feedback")
output.mkdir(parents=True, exist_ok=True)
stem = Path("output/captures/local")
report = dict(single=replay(stem/"DDGI-Feedback_capture.rdc", False),
              multi=replay(stem/"DDGI-Feedback_capture_2.rdc", True))
(output/"replay.json").write_text(json.dumps(report, indent=2))
sys.exit(0)
