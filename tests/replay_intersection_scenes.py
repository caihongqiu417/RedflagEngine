"""RF_RAY_WORK 指向单个正式桌面的工作目录，通过 qrenderdoc --python 回放。"""
import hashlib
import json
import os
from pathlib import Path
from statistics import median
import struct
import sys
import traceback

import renderdoc as rd


def actions(roots):
    for action in roots:
        yield action
        yield from actions(action.children)


def inspect(frame, work):
    stages = {}
    calls = list(actions(frame.GetRootActions()))
    dispatches = [a for a in calls if a.flags & rd.ActionFlags.Dispatch]
    buffers = {b.resourceId: b for b in frame.GetBuffers()}
    structure = frame.GetStructuredFile()
    scene = {}
    for a in dispatches:
        frame.SetFrameEvent(a.eventId, True)
        state = frame.GetPipelineState()
        shader = state.GetShaderReflection(rd.ShaderStage.Compute)
        slots = {shader.readWriteResources[u.access.index].fixedBindNumber: u.descriptor.resource
                 for u in state.GetReadWriteResources(rd.ShaderStage.Compute)}
        if set(slots) != set(range(len(slots))) or not set(slots.values()) <= buffers.keys():
            continue
        threads = tuple(shader.dispatchThreadsDimension)
        constants = [b.byteSize for b in shader.constantBlocks]
        push = bytes(frame.GetVulkanPipelineState().pushconsts)
        if threads == (64, 1, 1) and constants == [20] and len(slots) in (14, 15, 16):
            stage, round_, samples, flags, seed = struct.unpack_from("<5I", push)
            if stage not in (3, 9, 10):
                continue
            name, target = {3: ("direct", 8), 9: ("probe", 13), 10: ("ddgi", 8)}[stage]
            values = struct.unpack_from("<8I", frame.GetBufferData(slots[9], 112, 32))
            if not scene:
                scene = dict(size=values[:2], lights=values[2], nodes=values[3], grid=values[4:7],
                             triangles=buffers[slots[1]].length // 64)
                def node(index):
                    lo_x, lo_y, lo_z, _, hi_x, hi_y, hi_z, _, start, count, end, _ = struct.unpack(
                        "<8f4I", frame.GetBufferData(slots[0], index * 48, 48))
                    return dict(low=[lo_x, lo_y, lo_z], high=[hi_x, hi_y, hi_z], first=start, count=count, end=end)
                if scene["nodes"] > 1:
                    left = node(1)
                    scene["root_children"] = [left, node(left["end"])]
                triangles = bytes(frame.GetBufferData(slots[1], 0, 0))
                (work / "rays-triangles.f32").write_bytes(triangles)
                # GI 的两个变体都有 15 个缓冲区。镂空面的 UV 编号带负号，自发光的普通面仍使用正号。
                has_mask = len(slots) == 16 or (len(slots) == 15 and
                    any(face < 0 for (face,) in struct.iter_unpack("<60xf", triangles)))
                if has_mask:
                    scene["alpha_mask"] = True
            parameters = dict(stage=stage, frame=round_, samples=samples, flags=flags, seed=seed)
            if scene.get("alpha_mask"):
                parameters["alpha_mask"] = True
        elif threads == (64, 1, 1) and constants == [20] and len(slots) in (7, 10):
            density, albedo, g, distance, steps = struct.unpack_from("<4fi", push)
            name, target = "fog", 6
            parameters = dict(density=density, albedo=albedo, g=g, max_distance=distance, steps=steps)
            if len(slots) == 10:
                parameters["alpha_mask"] = True
            (work / "rays-fog-input.f32").write_bytes(frame.GetBufferData(slots[5], 0, 0))
        elif threads == (8, 8, 1) and not constants and len(slots) in (7, 10):
            # TAA 也有 7 个缓冲区。它的第 5、6 项存 RGBA；分簇点光源在这两项读包围盒和三角形。
            types = {r.fixedBindNumber: r.variableType for r in shader.readWriteResources}
            if types[5].members[0].type.arrayByteStride != 48 or types[6].members[0].type.arrayByteStride != 64:
                continue
            name, target = "cluster", 4
            parameters = dict(alpha_mask=len(slots) == 10)
        else:
            continue
        raw = frame.GetBufferData(slots[target], 0, 0)
        (work / ("rays-" + name + ".f32")).write_bytes(raw)
        assert name not in stages, "Unexpected second dispatch of " + name
        stages[name] = dict(event=a.eventId, groups=list(a.dispatchDimension), parameters=parameters,
                            output_bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest(),
                            captured_cpu_dispatch_us=structure.chunks[a.events[-1].chunkIndex].metadata.durationMicro)
    assert {"ddgi", "fog"} <= set(stages) and ("direct" in stages or "cluster" in stages), stages
    if os.environ.get("RF_RAY_TIMINGS") == "1":
        duration = rd.GPUCounter.EventGPUDuration
        assert duration in frame.EnumerateCounters()
        frame.FetchCounters([duration])
        for row in stages.values():
            row["gpu_ms"] = []
        for _ in range(11):
            times = {v.eventId: v.value.d * 1000 for v in frame.FetchCounters([duration])}
            for row in stages.values():
                row["gpu_ms"].append(times[row["event"]])
        for row in stages.values():
            row["median_ms"] = median(row["gpu_ms"])
    info = frame.GetFrameInfo()
    assert info.frameNumber == 24 and not info.debugMessages
    return dict(scene=scene, renderdoc=rd.GetVersionString(), frame=info.frameNumber,
                draws=sum(1 for a in calls if a.flags & rd.ActionFlags.Drawcall), dispatches=len(dispatches),
                stages=stages, debug_messages=[m.description for m in info.debugMessages])


def main():
    work = Path(os.environ["RF_RAY_WORK"]).resolve()
    path = Path((work / "frame-path.txt").read_text(encoding="utf8"))
    capture = rd.OpenCaptureFile()
    frame = None
    try:
        assert capture.OpenFile(str(path), "", None) == rd.ResultCode.Succeeded
        status, frame = capture.OpenCapture(rd.ReplayOptions(), None)
        assert status == rd.ResultCode.Succeeded
        report = inspect(frame, work)
        checksum = hashlib.sha256()
        with path.open("rb") as stream:
            for chunk in iter(lambda: stream.read(8 * 1024 * 1024), b""):
                checksum.update(chunk)
        report.update(capture=path.relative_to(Path.cwd()).as_posix(), capture_sha256=checksum.hexdigest())
        (work / "intersection.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf8")
    finally:
        if frame:
            frame.Shutdown()
        capture.Shutdown()


try:
    main()
except Exception:
    Path(os.environ["RF_RAY_WORK"], "intersection-error.txt").write_text(traceback.format_exc(), encoding="utf8")
    sys.exit(1)
sys.exit(0)
