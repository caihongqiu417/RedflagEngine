"""在 qrenderdoc 中回放天光场景，找到读取烘焙探针的那次计算。"""
import hashlib
import json
import os
from pathlib import Path
from statistics import median
import struct
import sys
import traceback

import renderdoc as rd


def dispatches(actions):
    for action in actions:
        if action.flags & rd.ActionFlags.Dispatch:
            yield action.eventId
        yield from dispatches(action.children)


def inspect(path):
    cap = rd.OpenCaptureFile()
    frame = None
    try:
        assert cap.OpenFile(str(path), "", None) == rd.ResultCode.Succeeded
        status, frame = cap.OpenCapture(rd.ReplayOptions(), None)
        assert status == rd.ResultCode.Succeeded
        stages = []
        events = list(dispatches(frame.GetRootActions()))
        assert frame.GetFrameInfo().frameNumber == 24, "需要正式场景面板的第 24 帧"
        for event in events:
            frame.SetFrameEvent(event, True)
            shader = frame.GetPipelineState().GetShaderReflection(rd.ShaderStage.Compute)
            if list(shader.dispatchThreadsDimension) == [64,1,1] and [b.byteSize for b in shader.constantBlocks] == [20]:
                stage, = struct.unpack_from("<I", bytes(frame.GetVulkanPipelineState().pushconsts))
                stages.append((event, stage))
        gather, = [event for event, stage in stages if stage == 10]
        assert not any(stage == 9 for event, stage in stages), "该帧重新烘焙了探针"
        counter = rd.GPUCounter.EventGPUDuration
        assert counter in frame.EnumerateCounters()
        frame.FetchCounters([counter])
        ms = [next(s.value.d * 1000 for s in frame.FetchCounters([counter]) if s.eventId == gather) for _ in range(9)]
        messages = [m.description for m in frame.GetFrameInfo().debugMessages]
        assert not messages
        with path.open("rb") as raw:
            digest = hashlib.file_digest(raw, "sha256").hexdigest() if hasattr(hashlib, "file_digest") else hashlib.sha256(raw.read()).hexdigest()
        return dict(frame=frame.GetFrameInfo().frameNumber, renderdoc=rd.GetVersionString(), frame_sha256=digest,
                    dispatches=events, gather_event=gather, gather_median_gpu_ms=median(ms), gather_gpu_ms=ms,
                    probe_update_events=[], debug_messages=messages)
    finally:
        if frame:
            frame.Shutdown()
        cap.Shutdown()


try:
    path = Path(os.environ["RF_SKY_CAPTURE"])
    result = inspect(path)
    if "RF_SKY_OTHER_CAPTURE" in os.environ:
        # 首版、新版、新版、首版。每次打开截帧先预热一次，再量同一条插值指令九次。
        other_path = Path(os.environ["RF_SKY_OTHER_CAPTURE"])
        other = inspect(other_path)
        for record, repeated in ((other, inspect(other_path)), (result, inspect(path))):
            assert record["frame_sha256"] == repeated["frame_sha256"] and record["gather_event"] == repeated["gather_event"]
            record["gather_gpu_ms"] += repeated["gather_gpu_ms"]
            record["gather_median_gpu_ms"] = median(record["gather_gpu_ms"])
            record["replay_order"] = ["first", "revised", "revised", "first"]
        Path(os.environ["RF_SKY_OTHER_TIMING"]).write_text(json.dumps(other, indent=2) + "\n", encoding="utf8")
except Exception:
    result = {"error": traceback.format_exc()}
Path(os.environ["RF_SKY_TIMING"]).write_text(json.dumps(result, indent=2) + "\n", encoding="utf8")
sys.exit(int("error" in result))
