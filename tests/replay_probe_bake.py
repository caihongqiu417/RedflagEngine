"""Run with qrenderdoc --python tests/replay_probe_bake.py from the repository root."""
from hashlib import sha256
import json
from pathlib import Path
from statistics import median
import sys

import renderdoc as rd


def dispatches(actions):
    for action in actions:
        if action.flags & rd.ActionFlags.Dispatch:
            yield action
        yield from dispatches(action.children)


def replay(mode, stages):
    path = Path("output/captures/local") / f"DDGI-{mode}_capture.rdc"
    cap = rd.OpenCaptureFile()
    ctrl = None
    try:
        assert cap.OpenFile(str(path), "", None) == rd.ResultCode.Succeeded
        result, ctrl = cap.OpenCapture(rd.ReplayOptions(), None)
        assert result == rd.ResultCode.Succeeded
        events = list(dispatches(ctrl.GetRootActions()))
        assert len(events) == len(stages)
        ids = {e.eventId: [] for e in events}
        for _ in range(9):
            for sample in ctrl.FetchCounters([rd.GPUCounter.EventGPUDuration]):
                if sample.eventId in ids:
                    ids[sample.eventId].append(sample.value.d * 1000)
        return dict(path=path.as_posix(), sha256=sha256(path.read_bytes()).hexdigest(),
                    messages=[m.description for m in ctrl.GetFrameInfo().debugMessages],
                    dispatches=[dict(stage=stage, event=e.eventId, groups=list(e.dispatchDimension),
                                     gpu_median_ms=median(ids[e.eventId]), gpu_ms=ids[e.eventId])
                                for e, stage in zip(events, stages)])
    finally:
        if ctrl:
            ctrl.Shutdown()
        cap.Shutdown()


if __name__ == "__main__":
    report = dict(renderdoc=rd.GetVersionString(), live=replay("Live", ["update", "gather"]),
                  baked=replay("Baked", ["gather"]))
    output = Path("output/profiling/probe-bake/renderdoc.json")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf8")
    sys.exit(0)
