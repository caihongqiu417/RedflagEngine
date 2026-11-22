"""在仓库根目录用 qrenderdoc --python tests/replay_emissive_gi.py 核对离屏灯牌。"""
from pathlib import Path
import hashlib
import json
import os
import struct
import sys

import renderdoc as rd


def dispatches(nodes):
    for node in nodes:
        if node.flags & rd.ActionFlags.Dispatch:
            yield node
        yield from dispatches(node.children)


path = Path(os.environ.get("RF_EMISSION_CAPTURE", "output/captures/local/OffscreenEmitter_capture.rdc"))
cap = rd.OpenCaptureFile()
assert cap.OpenFile(str(path), "", None) == rd.ResultCode.Succeeded
status, controller = cap.OpenCapture(rd.ReplayOptions(), None)
assert status == rd.ResultCode.Succeeded
try:
    events = []
    for action in dispatches(controller.GetRootActions()):
        controller.SetFrameEvent(action.eventId, True)
        data = controller.GetVulkanPipelineState().pushconsts
        stage, frame, samples, _, seed = struct.unpack_from("<5I", bytes(data))
        events.append({"event": action.eventId, "stage": stage, "frame_from_zero": frame,
                       "groups": list(action.dispatchDimension), "samples": samples, "seed": seed})
    assert [e["stage"] for e in events] == [6, 8]
    pipeline = controller.GetPipelineState()
    reflection = pipeline.GetShaderReflection(rd.ShaderStage.Compute)
    buffers = {reflection.readWriteResources[v.access.index].fixedBindNumber: v.descriptor.resource
               for v in pipeline.GetReadWriteResources(rd.ShaderStage.Compute)}
    pixels = list(struct.iter_unpack("<4f", controller.GetBufferData(buffers[8], 0, 17*17*16)))
    messages = [m.description for m in controller.GetFrameInfo().debugMessages]
    assert len(pixels) == 289 and not messages and max(p[2] for p in pixels) == 0
    result = {"capture": str(path.as_posix()), "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
              "tool": rd.GetVersionString(), "api": str(controller.GetAPIProperties().pipelineType),
              "events": events, "messages": messages,
              "mean_rgb_last_frame": [sum(p[k] for p in pixels)/len(pixels) for k in range(3)]}
    folder = Path("output/profiling/emissive-gi")
    folder.mkdir(exist_ok=True, parents=True)
    (folder/"renderdoc.json").write_text(json.dumps(result, indent=2)+"\n", encoding="utf8")
finally:
    controller.Shutdown()
    cap.Shutdown()
sys.exit(0)
