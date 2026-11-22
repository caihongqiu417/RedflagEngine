"""在仓库根目录用 qrenderdoc --python tests/replay_scene_reflection_check.py 核对灯牌倒影。"""
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import traceback

import renderdoc as rd


def dispatches(actions):
    for action in actions:
        if action.flags & rd.ActionFlags.Dispatch:
            yield action
        yield from dispatches(action.children)


rough = os.environ.get("RF_ROUGH_CAPTURE")
path = Path(rough or os.environ.get("RF_MIRROR_CAPTURE", "output/captures/local/scene-reflection-test-ideal/Mirror_capture.rdc"))
folder = Path("output/profiling/rough-reflections" if rough else "output/profiling/scene-reflections")
folder.mkdir(parents=True, exist_ok=True)
capture = rd.OpenCaptureFile()
controller = None
try:
    assert capture.OpenFile(str(path), "", None) == rd.ResultCode.Succeeded
    status, controller = capture.OpenCapture(rd.ReplayOptions(), None)
    assert status == rd.ResultCode.Succeeded
    calls = list(dispatches(controller.GetRootActions()))
    assert len(calls) == (2 if rough else 1)
    stage = rd.ShaderStage.Compute
    width = height = 96
    events = []
    previous = None
    for action, binding, filename in zip(calls, (3, 4), ("test-raw.f32", "test-filtered.f32") if rough else ("mirror.f32",)):
        controller.SetFrameEvent(action.eventId, True)
        state = controller.GetPipelineState()
        shader = state.GetShaderReflection(stage)
        slots = {shader.readWriteResources[r.access.index].fixedBindNumber: r.descriptor.resource
                 for r in state.GetReadWriteResources(stage)}
        if previous is not None: assert previous == slots[3]
        previous = slots[binding]
        pixels = bytes(controller.GetBufferData(previous, 0, width * height * 16))
        assert pixels == (path.parent / filename).read_bytes()
        events.append(dict(event=action.eventId, groups=list(action.dispatchDimension), buffer=filename,
                           rgba32f_sha256=hashlib.sha256(pixels).hexdigest()))
    messages = [m.description for m in controller.GetFrameInfo().debugMessages]
    assert not messages, messages
    result = dict(capture=path.as_posix(), capture_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                  tool=rd.GetVersionString(), api=str(controller.GetAPIProperties().pipelineType),
                  event=action.eventId, groups=list(action.dispatchDimension), width=width, height=height,
                  rgba32f_sha256=hashlib.sha256(pixels).hexdigest(), same_as_live_readback=True, messages=messages,
                  stripes={str(y): struct.unpack_from("<4f", pixels, (y*width+48)*16) for y in (31, 48, 14)})
    if rough: result["passes"] = events
except Exception:
    result = {"error": traceback.format_exc()}
finally:
    if controller:
        controller.Shutdown()
    capture.Shutdown()
    (folder / "test-capture.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf8")
sys.exit(1 if "error" in result else 0)
