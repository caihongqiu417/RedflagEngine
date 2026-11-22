"""Run from the repository root with qrenderdoc --python tests/replay_material_height.py."""
import hashlib
import json
import sys
import traceback
from pathlib import Path
from statistics import median, quantiles

import renderdoc as rd


def draws(nodes):
    for node in nodes:
        if node.flags & rd.ActionFlags.Drawcall:
            yield node
        yield from draws(node.children)


def run():
    modes = ["off", "bump", "parallax", "displacement"]
    result = {"renderdoc": rd.GetVersionString(), "order": modes + modes[::-1], "units": "microseconds", "cases": {}}
    for mode in result["order"]:
        file = Path("output/captures/local/height") / (mode + "_capture.rdc")
        cap = rd.OpenCaptureFile()
        assert cap.OpenFile(str(file.resolve()), "", None) == rd.ResultCode.Succeeded
        status, ctrl = cap.OpenCapture(rd.ReplayOptions(), None)
        assert status == rd.ResultCode.Succeeded
        try:
            mesh, = draws(ctrl.GetRootActions())
            assert mesh.numIndices == 24576 and mesh.numInstances == 1
            result["api"] = str(ctrl.GetAPIProperties().pipelineType)
            case = result["cases"].setdefault(mode, {"capture": file.as_posix(),
                "sha256": hashlib.sha256(file.read_bytes()).hexdigest(), "event": mesh.eventId,
                "indices": mesh.numIndices, "instances": mesh.numInstances, "samples_us": []})
            case["messages"] = [m.description for m in ctrl.GetDebugMessages()
                if m.severity in (rd.MessageSeverity.High, rd.MessageSeverity.Medium)]
            counter = rd.GPUCounter.EventGPUDuration
            assert counter in ctrl.EnumerateCounters()
            ctrl.FetchCounters([counter])
            for _ in range(6):
                measurement, = [v for v in ctrl.FetchCounters([counter]) if v.eventId == mesh.eventId]
                case["samples_us"].append(measurement.value.d * 1e6)
            values = case["samples_us"]
            deciles = quantiles(values, n=10, method="inclusive")
            case.update(p10=deciles[0], median=median(values), p90=deciles[-1])
        finally:
            ctrl.Shutdown()
            cap.Shutdown()
    return result


try:
    report = run()
except Exception:
    report = {"error": traceback.format_exc()}
Path("output/validation/material-height/replay.json").write_text(
    json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
sys.exit(int("error" in report))
