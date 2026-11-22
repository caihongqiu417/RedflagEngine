"""在仓库根目录用 RenderDoc 1.47 的 qrenderdoc --python tests/replay_directional_light.py 运行。"""
import csv
import hashlib
import json
import math
import struct
import sys
from pathlib import Path

import renderdoc as rd


def dispatches(actions):
    for action in actions:
        if action.flags & rd.ActionFlags.Dispatch:
            yield action
        yield from dispatches(action.children)


folder = Path("output/validation/directional")
source = folder / "DirectionalLight_capture.rdc"
live = {r["method"]: r for r in csv.DictReader((folder / "directional.csv").open())
        if r["case"] == "sun_shadow_on"}
capture = rd.OpenCaptureFile()
assert capture.OpenFile(str(source), "", None) == rd.ResultCode.Succeeded
status, replay = capture.OpenCapture(rd.ReplayOptions(), None)
assert status == rd.ResultCode.Succeeded
try:
    calls = list(dispatches(replay.GetRootActions()))
    stages = (("all", 8), ("cluster_cull", None), ("clustered", 4),
              ("restir_candidates", None), ("restir_di", 8), ("fog", 6))
    assert len(calls) == len(stages)
    report = dict(api=str(replay.GetAPIProperties().pipelineType), tool=rd.GetVersionString(),
                  capture=source.name, sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                  width=48, height=16, events=[],
                  debug_messages=[m.description for m in replay.GetFrameInfo().debugMessages])
    for action, (name, binding) in zip(calls, stages):
        record = dict(stage=name, event=action.eventId, groups=list(action.dispatchDimension))
        report["events"].append(record)
        if binding is None:
            continue
        replay.SetFrameEvent(action.eventId, True)
        state = replay.GetPipelineState()
        shader = state.GetShaderReflection(rd.ShaderStage.Compute)
        result = next(slot.descriptor.resource for slot in state.GetReadWriteResources(rd.ShaderStage.Compute)
                      if shader.readWriteResources[slot.access.index].fixedBindNumber == binding)
        data = bytes(replay.GetBufferData(result, 0, 48 * 16 * 16))
        samples = list(struct.iter_unpack("<4f", data))
        assert len(samples) == 48 * 16
        error = 0.
        for i, pixel in enumerate(samples):
            x, y = i % 48, i // 48
            distance = (3. if y < 8 else 11.) - .1
            for c, (base, light) in enumerate(zip((.8, .5, .2), (2., 1., .5))):
                # 假设本次截帧的方向光开启遮挡，左边各像素都收不到太阳光。
                if name == "fog":
                    phase = (1.-.4**2) / (4.*math.pi*(1.+.4)**3)
                    expected = light * .8 * phase * -math.expm1(-.15*distance)
                else:
                    expected = light * (.96*base+.01) / math.pi
                assert math.isfinite(pixel[c])
                error = max(error, abs(pixel[c] - (expected if x >= 24 else 0.)))
        record["max_abs_rgb_error"] = error
        record["four_red_samples"] = [samples[48*y+x][0] for y in (4, 12) for x in (12, 36)]
        keys = ("left_near_red", "right_near_red", "left_far_red", "right_far_red")
        assert all(abs(p-float(live[name][k])) < 1.e-6 for k, p in zip(keys, record["four_red_samples"]))
        assert error < 2.e-5
    assert not report["debug_messages"], report["debug_messages"]
    (folder / "replay.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf8")
finally:
    replay.Shutdown()
    capture.Shutdown()
sys.exit(0)
