"""从仓库根目录运行 qrenderdoc --python tests/replay_texture_minification.py。"""
import hashlib
import json
from pathlib import Path
import struct
import sys

import renderdoc as rd


def draws(actions):
    for action in actions:
        if action.flags & rd.ActionFlags.Drawcall:
            yield action
        yield from draws(action.children)


def frame(controller):
    draw, = draws(controller.GetRootActions())
    controller.SetFrameEvent(draw.eventId, True)
    pipeline = controller.GetPipelineState()
    stage = rd.ShaderStage.Pixel
    shader = pipeline.GetShaderReflection(stage)
    textures = {texture.resourceId: texture for texture in controller.GetTextures()}
    bindings = {}
    for used in pipeline.GetReadOnlyResources(stage):
        binding = shader.readOnlyResources[used.access.index]
        if binding.fixedBindSetOrSpace not in (0, 3):
            continue
        texture = textures[used.descriptor.resource]
        record = dict(id=str(texture.resourceId), format=texture.format.Name(), width=texture.width,
                      height=texture.height, levels=texture.mips)
        if texture.mips > 1:
            raw = controller.GetTextureData(texture.resourceId, rd.Subresource(1, 0, 0))
            record["mip_1_rgba_bytes_first_texel"] = list(bytes(raw[:4]))
        bindings[str(binding.fixedBindSetOrSpace)] = record

    color = pipeline.GetOutputTargets()[0].resource
    raw = controller.GetTextureData(color, rd.Subresource(0, 0, 0))
    return dict(eid=draw.eventId, bindings=bindings, output_levels=textures[color].mips,
                pixel=struct.unpack_from("<4f", raw, (72*256+128)*16),
                messages=[msg.description for msg in controller.GetFrameInfo().debugMessages])


def main():
    root = Path("output/profiling/texture-minification").resolve()
    report = dict(renderdoc=rd.GetVersionString(), extent=[256, 144], pixel_xy=[128, 72])
    for variant in ("old", "fixed"):
        path = root/variant/"Floor_capture.rdc"
        capture = rd.OpenCaptureFile()
        controller = None
        try:
            assert capture.OpenFile(str(path), "", None) == rd.ResultCode.Succeeded
            status, controller = capture.OpenCapture(rd.ReplayOptions(), None)
            assert status == rd.ResultCode.Succeeded
            result = frame(controller)
            result.update(file=path.relative_to(root).as_posix(), sha256=hashlib.sha256(path.read_bytes()).hexdigest())
            report[variant] = result
        finally:
            if controller:
                controller.Shutdown()
            capture.Shutdown()
    (root/"renderdoc.json").write_text(json.dumps(report, indent=2), encoding="utf-8")


try:
    main()
except Exception:
    import traceback
    traceback.print_exc()
    sys.exit(1)
sys.exit(0)
