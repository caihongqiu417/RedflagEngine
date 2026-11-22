"""在仓库根目录用 qrenderdoc --python tests/replay_metal_furnace.py 逐像素核对 Vulkan 截帧。"""

import hashlib
import json
import struct
import sys
import traceback
from pathlib import Path

import renderdoc as rd


def dispatches(actions):
    for action in actions:
        if action.flags & rd.ActionFlags.Dispatch:
            yield action
        yield from dispatches(action.children)


folder = Path("output/validation/metal-furnace")
path = folder / "furnace.rdc"
capture = rd.OpenCaptureFile()
controller = None
try:
    assert capture.OpenFile(str(path), "", None) == rd.ResultCode.Succeeded
    status, controller = capture.OpenCapture(rd.ReplayOptions(), None)
    assert status == rd.ResultCode.Succeeded
    (draw,) = dispatches(controller.GetRootActions())
    controller.SetFrameEvent(draw.eventId, True)
    state = controller.GetPipelineState()
    stage = rd.ShaderStage.Compute
    shader = state.GetShaderReflection(stage)
    slots = {
        shader.readWriteResources[r.access.index].fixedBindNumber: r.descriptor.resource
        for r in state.GetReadWriteResources(stage)
    }

    width, height = 260, 195
    pixels = bytes(controller.GetBufferData(slots[3], 0, width * height * 16))
    assert pixels == Path("bin-int/Tests/metal-furnace/vulkan-after.rgba").read_bytes()
    assert list(draw.dispatchDimension) == [33, 25, 1]
    warnings = [m.description for m in controller.GetFrameInfo().debugMessages]
    assert not warnings, warnings
    result = {
        "renderdoc": rd.GetVersionString(),
        "api": str(controller.GetAPIProperties().pipelineType),
        "event": draw.eventId,
        "groups": list(draw.dispatchDimension),
        "extent": [width, height],
        "color_buffer_sha256": hashlib.sha256(pixels).hexdigest(),
        "capture_sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        "rough_white_center": struct.unpack_from("<3f", pixels, (32 * width + 227) * 16),
        "matches_live_buffer": True,
        "validation_messages": warnings,
    }
except Exception:
    result = {"error": traceback.format_exc()}
finally:
    if controller:
        controller.Shutdown()
    capture.Shutdown()
    (folder / "replay.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf8")
sys.exit(1 if "error" in result else 0)
