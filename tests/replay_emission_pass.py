"""用 qrenderdoc --python 运行。RF_EMISSION_BEFORE、RF_EMISSION_AFTER 指向两次编辑器截帧的工作目录。"""
import hashlib
import json
import os
from pathlib import Path
from statistics import median
import sys
import traceback

import renderdoc as rd


def actions(roots):
    for action in roots:
        yield action
        yield from actions(action.children)


def inspect(frame, work):
    textures = {t.resourceId: t for t in frame.GetTextures()}
    passes = {}
    dispatches = []
    draw_durations = {}
    structure = frame.GetStructuredFile()
    for action in actions(frame.GetRootActions()):
        if action.flags & rd.ActionFlags.Dispatch:
            dispatches.append(action.eventId)
            continue
        if not action.flags & rd.ActionFlags.Drawcall:
            continue
        frame.SetFrameEvent(action.eventId, True)
        state = frame.GetPipelineState()
        images = [textures[d.resource] for d in state.GetOutputTargets() if d.resource in textures]
        if images[0].creationFlags & rd.TextureCategory.SwapBuffer:
            name = "ui"
        elif len(images) == 1:
            name = "emission"
        elif images[0].format.Name() == "R32G32B32A32_FLOAT":
            name = "surfaces" if len(images) == 2 else "transparent"
        else:
            name = "picking"
        z = state.GetDepthTarget().resource
        row = passes.setdefault(name, dict(draws=[], indices=0, color=str(images[0].resourceId),
                                           depth=str(z), format=images[0].format.Name(),
                                           depth_format=textures[z].format.Name() if z in textures else None,
                                           size=[images[0].width, images[0].height],
                                           vertex_stride=state.GetVBuffers()[0].byteStride))
        row["draws"].append(action.eventId)
        row["indices"] += action.numIndices * action.numInstances
        chunk = structure.chunks[action.events[-1].chunkIndex]
        draw_durations[action.eventId] = chunk.metadata.durationMicro
    emission = passes["emission"]
    frame.SetFrameEvent(emission["draws"][-1], True)
    target = frame.GetPipelineState().GetOutputTargets()[0].resource
    (work / "emission.f32").write_bytes(frame.GetTextureData(target, rd.Subresource()))
    emission["captured_draw_api_us"] = [draw_durations[e] for e in emission["draws"]]
    add = None
    for eid in dispatches:
        frame.SetFrameEvent(eid, True)
        used = frame.GetPipelineState().GetReadWriteResources(rd.ShaderStage.Compute)
        if any(str(r.descriptor.resource) == emission["color"] for r in used):
            add = eid
            break
    assert add and frame.GetFrameInfo().frameNumber == 24
    result = dict(passes=passes, dispatches=dispatches, add_emission=add,
                  debug=[m.description for m in frame.GetFrameInfo().debugMessages])
    if os.environ.get("RF_EMISSION_TIMINGS") == "1":
        duration = rd.GPUCounter.EventGPUDuration
        assert duration in frame.EnumerateCounters()
        frame.FetchCounters([duration])
        draws, combine = [], []
        for _ in range(11):
            values = {v.eventId: v.value.d * 1e6 for v in frame.FetchCounters([duration])}
            draws.append(sum(values[e] for e in emission["draws"]))
            combine.append(values[add])
        result["gpu_replay_us"] = dict(emission_draws=draws, add_emission=combine,
                                       median_draws=median(draws), median_add=median(combine))
    return result


def run():
    root = Path.cwd()
    out = root / "output/profiling/emissive-material"
    report = dict(tool=rd.GetVersionString(), frame=24, scene="Sun-Temple.redflag", samples={})
    file = out / "temple.json"
    previous = json.loads(file.read_text(encoding="utf8"))["samples"] if file.exists() else {}
    for case in ("before", "after"):
        work = Path(os.environ["RF_EMISSION_" + case.upper()])
        path = Path((work / "frame-path.txt").read_text(encoding="utf8"))
        capture = rd.OpenCaptureFile()
        frame = None
        try:
            assert capture.OpenFile(str(path), "", None) == rd.ResultCode.Succeeded
            status, frame = capture.OpenCapture(rd.ReplayOptions(), None)
            assert status == rd.ResultCode.Succeeded
            row = inspect(frame, work)
            with path.open("rb") as stream:
                sha = hashlib.sha256()
                for chunk in iter(lambda: stream.read(8 * 1024 * 1024), b""):
                    sha.update(chunk)
            row.update(capture=path.relative_to(root).as_posix(), capture_sha256=sha.hexdigest())
            old = previous.get(case, {})
            if old.get("capture_sha256") == row["capture_sha256"] and "gpu_replay_us" in old:
                row.setdefault("gpu_replay_us", old["gpu_replay_us"])
            for item in ("light", "display"):
                row[item + "_sha256"] = hashlib.sha256((work / (item + ".f32")).read_bytes()).hexdigest()
            report["samples"][case] = row
        finally:
            if frame:
                frame.Shutdown()
            capture.Shutdown()
    a, b = report["samples"].values()
    assert a["light_sha256"] == b["light_sha256"] and a["display_sha256"] == b["display_sha256"]
    for r in (a,b):
        assert not r["debug"]
    assert a["passes"]["surfaces"]["depth"] != a["passes"]["emission"]["depth"]
    assert b["passes"]["surfaces"]["depth"] == b["passes"]["emission"]["depth"]
    file.write_text(json.dumps(report, indent=2) + "\n", encoding="utf8")


try:
    run()
except Exception:
    (Path("tmp/emissive-profile") / "replay-error.txt").write_text(traceback.format_exc(), encoding="utf8")
    sys.exit(1)
sys.exit(0)
