"""Run from the repository root with qrenderdoc --python tests/replay_primary_surfaces.py."""
import json
from pathlib import Path
import sys

import renderdoc as rd


root = Path.cwd()
output = root / "output/profiling/primary-surfaces"
records = json.loads((output / "summary.json").read_text())
result = {}
for name, row in records.items():
    capture = rd.OpenCaptureFile()
    assert capture.OpenFile(str(root / row["capture"]), "", None) == rd.ResultCode.Succeeded
    status, controller = capture.OpenCapture(rd.ReplayOptions(), None)
    assert status == rd.ResultCode.Succeeded
    try:
        assert controller.GetFrameInfo().frameNumber == row["frame"]
        duration = rd.GPUCounter.EventGPUDuration
        assert duration in controller.EnumerateCounters()
        controller.FetchCounters([duration])
        samples = []
        for _ in range(11):
            times = {event.eventId: event.value.d * 1000 for event in controller.FetchCounters([duration])}
            samples.append(sum(times[event] for event in row["events"]))
        result[name] = samples
    finally:
        controller.Shutdown()
        capture.Shutdown()

(output / "latest-ms.json").write_text(json.dumps(result, indent=2))
sys.exit(0)
