"""Interleave the two complete rack frames and report which GPU commands account for the difference."""
import json
import os
from pathlib import Path
from statistics import median
import sys
import traceback

import renderdoc as rd


def visit(nodes):
    for a in nodes:
        yield a
        yield from visit(a.children)


def run():
    order=[view+"-"+variant for variant in ("original","current","current","original")]
    result={"order":order,"units":"microseconds","cases":{}}
    for label in order:
        report=json.loads((out/(label+".json")).read_text(encoding="utf8"))
        file=Path(report["capture"]).resolve(strict=True)
        cap=rd.OpenCaptureFile()
        assert cap.OpenFile(str(file),"",None)==rd.ResultCode.Succeeded
        status,ctrl=cap.OpenCapture(rd.ReplayOptions(),None)
        assert status==rd.ResultCode.Succeeded
        try:
            acts={a.eventId:a for a in visit(ctrl.GetRootActions())
                  if a.flags&(rd.ActionFlags.Drawcall|rd.ActionFlags.Dispatch)}
            counter=rd.GPUCounter.EventGPUDuration
            ctrl.FetchCounters([counter])
            case=result["cases"].setdefault(label,{"material":[],"sum":[],"slowest":[]})
            for _ in range(5):
                samples={d.eventId:d.value.d*1e6 for d in ctrl.FetchCounters([counter]) if d.eventId in acts}
                case["material"].append(samples[report["material_draws"][0]["event"]])
                case["sum"].append(sum(samples.values()))
                case["slowest"].append([{"event":e,"us":samples[e],
                    "kind":"dispatch" if acts[e].flags&rd.ActionFlags.Dispatch else "draw",
                    "groups":list(acts[e].dispatchDimension),"indices":acts[e].numIndices}
                    for e in sorted(samples,key=samples.get,reverse=True)[:4]])
            case["median_material_us"]=median(case["material"])
            case["median_sum_us"]=median(case["sum"])
        finally:
            ctrl.Shutdown()
            cap.Shutdown()
    return result


view=os.environ.get("RF_COST_VIEW","rack")
out=Path("output/validation")/("station-tracks" if view=="tracks" else "station-materials")
try:
    data=run()
except Exception:
    data={"error":traceback.format_exc()}
(out/(view+"-timing.json")).write_text(json.dumps(data,indent=2)+"\n",encoding="utf8")
sys.exit(int("error" in data))
