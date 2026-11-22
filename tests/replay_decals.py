"""用 qrenderdoc --python 回放泥渍与油漆的原始 Vulkan 帧，按像素读回投影前后的材质。"""
import hashlib
import json
import os
import struct
import sys
import traceback
from pathlib import Path

import renderdoc as rd


def actions(nodes):
    for node in nodes:
        yield node
        yield from actions(node.children)


def inspect(frame):
    events = []
    all_actions = list(actions(frame.GetRootActions()))
    samples = {"floor": (289, 267), "wall": (404, 106), "background": (20, 20)}
    for action in all_actions:
        if not action.flags & rd.ActionFlags.Dispatch:
            continue
        frame.SetFrameEvent(action.eventId, True)
        pipe = frame.GetPipelineState()
        stage = rd.ShaderStage.Compute
        shader = pipe.GetShaderReflection(stage)
        if tuple(shader.dispatchThreadsDimension) != (8, 8, 1) or [b.byteSize for b in shader.constantBlocks] != [128]:
            continue
        resources = {shader.readWriteResources[r.access.index].fixedBindNumber: r.descriptor.resource
                     for r in pipe.GetReadWriteResources(stage)}
        if set(resources) != {0, 1}:
            continue
        push = bytes(frame.GetVulkanPipelineState().pushconsts)
        width, height, left, top = struct.unpack_from("<4I", push, 112)
        assert (width, height) == (640, 416)
        entry = {"event": action.eventId, "dispatch_groups": list(action.dispatchDimension),
                 "screen_origin": [left, top], "tint": struct.unpack_from("<4f", push, 64), "pixels": {}}
        for name, (x, y) in samples.items():
            offset = (y * width + x) * 48
            pixel = entry["pixels"][name] = {"xy": [x, y]}
            for label, number in (("before", action.eventId-1), ("after", action.eventId)):
                frame.SetFrameEvent(number, True)
                values = struct.unpack("<12f", bytes(frame.GetBufferData(resources[0], offset, 48)))
                pixel[label] = {"position": values[:4], "normal": values[4:7], "roughness": values[7],
                                "albedo_linear": values[8:11], "metallic": values[11]}
        events.append(entry)
    assert len(events) == 2, str(events)
    for event, expected in zip(events, ("floor", "wall")):
        for name, pixel in event["pixels"].items():
            before, after = pixel["before"], pixel["after"]
            assert before["normal"] == after["normal"] and before["position"] == after["position"]
            assert (before["albedo_linear"] != after["albedo_linear"]) == (name == expected)
    return {"api": str(frame.GetAPIProperties().pipelineType), "renderdoc": rd.GetVersionString(), "size": [640, 416],
            "draws": sum(bool(a.flags & rd.ActionFlags.Drawcall) for a in all_actions),
            "dispatches": sum(bool(a.flags & rd.ActionFlags.Dispatch) for a in all_actions), "projectors": events,
            "messages_medium_or_high": [m.description for m in frame.GetDebugMessages()
                                        if m.severity in (rd.MessageSeverity.High, rd.MessageSeverity.Medium)]}


def replay(path):
    cap = rd.OpenCaptureFile()
    assert cap.OpenFile(str(path), "", None) == rd.ResultCode.Succeeded
    ok, frame = cap.OpenCapture(rd.ReplayOptions(), None)
    assert ok == rd.ResultCode.Succeeded
    try:
        data = inspect(frame)
        data.update(capture=path.name, sha256=hashlib.sha256(path.read_bytes()).hexdigest())
        return data
    finally:
        frame.Shutdown()
        cap.Shutdown()


path = Path(os.environ.get("RF_DECAL_CAPTURE", "output/captures/Decals_capture.rdc")).resolve()
out = Path(os.environ.get("RF_DECAL_REPORT", "output/validation/decals/replay.json"))
try:
    data = replay(path)
except Exception:
    data = {"error": traceback.format_exc()}
out.write_text(json.dumps(data, indent=2) + "\n", encoding="utf8")
sys.exit(int("error" in data))
