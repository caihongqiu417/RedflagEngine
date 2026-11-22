"""在 qrenderdoc --python 中运行，从仓库根目录回放两种草地画法。"""
import hashlib
import json
import sys
import traceback
from pathlib import Path
from statistics import median

import renderdoc as rd

root = Path.cwd()
capdir = root / "output/captures/local/foliage"
out = root / "output/validation/foliage/replay.json"


def actions(nodes):
    for node in nodes:
        if node.flags & rd.ActionFlags.Drawcall:
            yield node
        yield from actions(node.children)


def run():
    result = {"renderdoc": rd.GetVersionString(), "units": "microseconds",
              "order": ["instanced", "separate", "separate", "instanced"], "cases": {}}
    for name in result["order"]:
        file = max(capdir.glob(name + "*.rdc"), key=lambda p: p.stat().st_mtime)
        cap = rd.OpenCaptureFile()
        assert cap.OpenFile(str(file), "", None) == rd.ResultCode.Succeeded
        ok, ctrl = cap.OpenCapture(rd.ReplayOptions(), None)
        assert ok == rd.ResultCode.Succeeded
        try:
            draws = list(actions(ctrl.GetRootActions()))
            vegetation = [d for d in draws if d.numIndices > 6]  # 地面是两个三角形的平面
            assert vegetation and len(vegetation) + 1 == len(draws)
            assert all(a.numInstances == (1 if name=="separate" else 36) for a in vegetation)
            events = [{"id": d.eventId, "indices": d.numIndices, "instances": d.numInstances} for d in draws]
            case = result["cases"].setdefault(name, {"capture":file.name,"sha256":hashlib.sha256(file.read_bytes()).hexdigest(),
                                                    "draws":events,"foliage_us":[],"all_draws_us":[]})
            props = ctrl.GetAPIProperties()
            result["api"] = str(props.pipelineType)
            messages = [str(msg.description) for msg in ctrl.GetDebugMessages() if msg.severity in
                        (rd.MessageSeverity.High, rd.MessageSeverity.Medium)]
            case["warnings"] = messages
            counter = rd.GPUCounter.EventGPUDuration
            assert counter in ctrl.EnumerateCounters()
            ctrl.FetchCounters([counter])  # 首轮只预热
            for _ in range(6):
                elapsed = {t.eventId:t.value.d*1e6 for t in ctrl.FetchCounters([counter])}
                case["foliage_us"].append(sum(elapsed[d.eventId] for d in vegetation))
                case["all_draws_us"].append(sum(elapsed[d.eventId] for d in draws))
            case["median_foliage_us"] = median(case["foliage_us"])
            case["median_all_draws_us"] = median(case["all_draws_us"])
        finally:
            ctrl.Shutdown()
            cap.Shutdown()
    return result


try:
    data = run()
except Exception:
    data = {"error":traceback.format_exc()}
out.write_text(json.dumps(data,indent=2,ensure_ascii=False)+"\n",encoding="utf8")
sys.exit(int("error" in data))
