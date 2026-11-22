"""在 RenderDoc 的 Python 环境回放两份中庭截帧。RF_WIRE_FRAMES 为两个绝对路径组成的 JSON 数组。"""
import hashlib
import json
import os
from pathlib import Path
from statistics import median, quantiles
import sys
import traceback

import renderdoc as rd


def walk(actions):
    for action in actions:
        yield action
        yield from walk(action.children)


def measure(path):
    capture = rd.OpenCaptureFile()
    frame = None
    try:
        assert capture.OpenFile(str(path), "", None) == rd.ResultCode.Succeeded
        status, frame = capture.OpenCapture(rd.ReplayOptions(), None)
        assert status == rd.ResultCode.Succeeded
        counter = rd.GPUCounter.EventGPUDuration
        assert counter in frame.EnumerateCounters()
        calls = list(walk(frame.GetRootActions()))
        draws = [a.eventId for a in calls if a.flags & rd.ActionFlags.Drawcall]
        computes = [a.eventId for a in calls if a.flags & rd.ActionFlags.Dispatch]
        assert len(computes) >= 2, "This frame only redisplayed a cached image"

        # 在本次 Lit 配置中，倒数第二次计算是 TAA，最后一次是显示转换。先核对着色器，再单独计时。
        frame.SetFrameEvent(computes[-2], True)
        shader = frame.GetPipelineState().GetShaderReflection(rd.ShaderStage.Compute)
        assert len(shader.readWriteResources) == 7
        frame.SetFrameEvent(computes[-1], True)
        shader = frame.GetPipelineState().GetShaderReflection(rd.ShaderStage.Compute)
        assert len(shader.readWriteResources) == 2
        groups = dict(draw_dispatch=draws + computes, compute=computes, taa=[computes[-2]], display=[computes[-1]])
        samples = {group: [] for group in groups}
        frame.FetchCounters([counter])
        for _ in range(6):
            timings = {t.eventId: t.value.d * 1000 for t in frame.FetchCounters([counter])}
            for group, events in groups.items():
                samples[group].append(sum(timings[event] for event in events))
        return samples, dict(draws=len(draws), dispatches=len(computes), taa=computes[-2], display=computes[-1])
    finally:
        if frame:
            frame.Shutdown()
        capture.Shutdown()


def run():
    paths = [Path(p) for p in json.loads(os.environ["RF_WIRE_FRAMES"])]
    cases = []
    for path in paths:
        digest = hashlib.sha256()
        with path.open("rb") as file:
            for block in iter(lambda: file.read(4 * 1024 * 1024), b""):
                digest.update(block)
        cases.append(dict(capture=path.parent.name + "/" + path.name, sha256=digest.hexdigest(), samples_ms={}))
    for index in [0, 1, 1, 0]:
        samples, events = measure(paths[index])
        case = cases[index]
        case["events"] = events
        for group, values in samples.items():
            case["samples_ms"].setdefault(group, []).extend(values)
    for case in cases:
        case["median_ms"] = {group: median(values) for group, values in case["samples_ms"].items()}
        case["p10_p90_ms"] = {}
        for group, values in case["samples_ms"].items():
            deciles = quantiles(values, n=10, method="inclusive")
            case["p10_p90_ms"][group] = [deciles[0], deciles[-1]]
    return dict(renderdoc=rd.GetVersionString(), counter="EventGPUDuration", order=[0, 1, 1, 0],
                warmups_per_visit=1, measurements_per_visit=6, cases=cases)


try:
    result = run()
except Exception:
    result = dict(error=traceback.format_exc())
Path(os.environ["RF_WIRE_TIMING"]).write_text(json.dumps(result, indent=2) + "\n", encoding="utf8")
sys.exit(int("error" in result))
