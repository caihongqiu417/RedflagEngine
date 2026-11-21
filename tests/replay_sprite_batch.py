"""Run qrenderdoc --python from the project root to replay the sprite submissions."""
import hashlib
import json
import struct
import sys
import traceback
from pathlib import Path

import renderdoc as rd

output = Path("output/profiling/batch2d")
frame = output / "Sprites_capture.rdc"
report = {"capture": frame.name, "sha256": hashlib.sha256(frame.read_bytes()).hexdigest(),
          "replay_version": rd.GetVersionString(), "api": "OpenGL", "target": [256, 256],
          "api_validation": True, "passes": {}}
capture = rd.OpenCaptureFile()
controller = None
subresource = rd.Subresource(0, 0, 0)


def draws(actions):
    for action in actions:
        if action.flags & rd.ActionFlags.Drawcall:
            yield action
        yield from draws(action.children)


def color_png(event, name):
    controller.SetFrameEvent(event, True)
    texture = controller.GetPipelineState().GetOutputTargets()[0].resource
    dest = rd.TextureSave()
    dest.resourceId = texture
    dest.destType = rd.FileType.PNG
    dest.mip = 0
    dest.slice.sliceIndex = 0
    assert controller.SaveTexture(dest, str(output / name)) == rd.ResultCode.Succeeded


def messages(entries):
    return [{"eid": e.eventId, "severity": str(e.severity), "message": e.description} for e in entries]


try:
    assert capture.OpenFile(str(frame), "", None) == rd.ResultCode.Succeeded
    options = rd.ReplayOptions()
    options.apiValidation = True
    status, controller = capture.OpenCapture(options, None)
    assert status == rd.ResultCode.Succeeded, str(status)
    actions = list(draws(controller.GetRootActions()))
    assert len(actions) == 65
    color_png(actions[31].eventId, "individual-half.png")
    report["partial_image"] = {"event": actions[31].eventId, "file": "individual-half.png"}
    images = {}
    for name, group in (("individual", actions[:-1]), ("batch", actions[-1:])):
        count = 6 if name == "individual" else 384
        assert all(a.numIndices == count and a.numInstances == 1 for a in group)
        last = group[-1].eventId
        color_png(last, name + ".png")
        targets = controller.GetPipelineState().GetOutputTargets()
        color = bytes(controller.GetTextureData(targets[0].resource, subresource))
        ids = bytes(controller.GetTextureData(targets[1].resource, subresource))
        assert len(color) == len(ids) == 256 * 256 * 4
        values = set(struct.unpack("<65536i", ids))
        assert values == {-1} | set(range(1, 65)), values
        # Sample both icon centers in top-left replay coordinates.
        centers = []
        for x, y in ((16, 16), (48, 16)):
            rgba = controller.PickPixel(targets[0].resource, x, y, subresource, rd.CompType.Typeless)
            entity = controller.PickPixel(targets[1].resource, x, y, subresource, rd.CompType.SInt)
            centers.append({"xy": [x, y], "entity": entity.intValue[0],
                            "rgba8": [round(c * 255) for c in rgba.floatValue]})
        assert centers[0]["rgba8"] != centers[1]["rgba8"], centers
        images[name] = color, ids
        report["passes"][name] = {
            "draw_events": [a.eventId for a in group], "indices_per_draw": count, "draws": len(group),
            "triangles": sum(a.numIndices // 3 for a in group), "instances_per_draw": 1,
            "rgba8_sha256": hashlib.sha256(color).hexdigest(), "r32i_sha256": hashlib.sha256(ids).hexdigest(),
            "attachment": name + ".png", "center_samples": centers}
    assert images["individual"] == images["batch"]
    report["match"] = "RGBA8 and R32I: 65536 of 65536 pixels are byte-identical on both attachments"
    report["capture_messages"] = messages(controller.GetFrameInfo().debugMessages)
    report["replay_messages"] = messages(controller.GetDebugMessages())
    report["status"] = "passed"
except BaseException:
    report["status"] = "failed"
    report["error"] = traceback.format_exc()
finally:
    if controller:
        controller.Shutdown()
    capture.Shutdown()
    (output / "replay.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
sys.exit(0 if report["status"] == "passed" else 1)
