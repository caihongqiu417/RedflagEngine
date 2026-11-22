"""Run qrenderdoc --python tests/replay_stanford_effects.py from the repository root."""
from pathlib import Path
import hashlib
import json
import sys
import traceback

import renderdoc as rd

SCENES = ("StanfordBunny", "StanfordDragon", "HappyBuddha")
STAGES = ("ddgi-probes", "ddgi", "gi-candidates", "restir-gi", "di-candidates", "restir-di",
          "direct", "ao", "ssr", "cluster-mask", "cluster", "taa")
OUTPUTS = {"direct": 8, "ao": 3, "ssr": 3, "cluster": 4}


def dispatches(actions):
    for action in actions:
        if action.flags & rd.ActionFlags.Dispatch:
            yield action
        yield from dispatches(action.children)


def examine(name):
    capture = rd.OpenCaptureFile()
    controller = None
    try:
        path = max(Path("output/profiling/stanford").glob(name + "*.rdc"), key=lambda p: p.stat().st_mtime)
        assert capture.OpenFile(str(path), "", None) == rd.ResultCode.Succeeded
        status, controller = capture.OpenCapture(rd.ReplayOptions(), None)
        assert status == rd.ResultCode.Succeeded
        actions = list(dispatches(controller.GetRootActions()))
        if len(actions) != len(STAGES):
            raise ValueError(f"{name}: expected {len(STAGES)} dispatches, found {len(actions)}")
        events = []
        for action, stage in zip(actions, STAGES):
            event = dict(stage=stage, eid=action.eventId, groups=list(action.dispatchDimension))
            if stage in OUTPUTS:
                controller.SetFrameEvent(action.eventId, True)
                state = controller.GetPipelineState()
                reflection = state.GetShaderReflection(rd.ShaderStage.Compute)
                resources = state.GetReadWriteResources(rd.ShaderStage.Compute)
                resource = next(r.descriptor.resource for r in resources
                                if reflection.readWriteResources[r.access.index].fixedBindNumber == OUTPUTS[stage])
                gpu = bytes(controller.GetBufferData(resource, 0, 288 * 192 * 16))
                raw = (Path("tmp/stanford-effects") / name / (stage + ".f32")).read_bytes()
                if gpu != raw:
                    raise ValueError(f"{name}/{stage}: replay differs from standalone Vulkan readback")
                event["verified_output_sha256"] = hashlib.sha256(gpu).hexdigest()
            events.append(event)
        return dict(capture=path.as_posix(), capture_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                    messages=[m.description for m in controller.GetFrameInfo().debugMessages], events=events)
    finally:
        if controller: controller.Shutdown()
        capture.Shutdown()


report = dict(renderdoc_version=rd.GetVersionString())
try:
    report["scenes"] = {name: examine(name) for name in SCENES}
except Exception:
    report["error"] = traceback.format_exc()
Path("output/scenes/stanford-vulkan/events.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
sys.exit(0)
