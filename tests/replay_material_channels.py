"""从仓库根目录用 qrenderdoc --python tests/replay_material_channels.py 回放材质色板。"""
import array
import json
import math
import os
from pathlib import Path
import struct
import sys
import traceback
import zlib

import renderdoc as rd


def actions(roots):
    for action in roots:
        yield action
        yield from actions(action.children)


def save_png(path, width, height, pixels, channels):
    def chunk(name, data):
        return struct.pack(">I", len(data)) + name + data + struct.pack(">I", zlib.crc32(name + data))

    stride = width * channels
    rows = b"".join(b"\0" + pixels[i:i+stride] for i in range(0, len(pixels), stride))
    header = struct.pack(">IIBBBBB", width, height, 8, 2 if channels == 3 else 0, 0, 0, 0)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))


def export(controller, out, frame, filename):
    textures = {t.resourceId: t for t in controller.GetTextures()}
    buffers = {b.resourceId: b for b in controller.GetBuffers()}
    draws = [a for a in actions(controller.GetRootActions()) if a.flags & rd.ActionFlags.Drawcall]
    for draw in draws:
        controller.SetFrameEvent(draw.eventId, True)
        outputs = [d.resource for d in controller.GetPipelineState().GetOutputTargets() if d.resource in textures]
        if len(outputs) == 2 and all(textures[i].format.Name() == "R32G32B32A32_FLOAT" for i in outputs):
            break
    else:
        raise ValueError("未找到绘制色板的两张浮点图")
    albedo_id, normal_depth_id = outputs
    width, height = textures[albedo_id].width, textures[albedo_id].height
    albedo = array.array("f", controller.GetTextureData(albedo_id, rd.Subresource(0, 0, 0)))

    dispatches = [a for a in actions(controller.GetRootActions()) if a.flags & rd.ActionFlags.Dispatch]
    for event in dispatches:
        controller.SetFrameEvent(event.eventId, True)
        bindings = controller.GetPipelineState().GetReadWriteResources(rd.ShaderStage.Compute)
        resources = [binding.descriptor.resource for binding in bindings]
        if albedo_id in resources and normal_depth_id in resources:
            surface_id = next(i for i in resources if i in buffers and buffers[i].length == width * height * 48)
            break
    else:
        raise ValueError("未找到把色板材质写入表面数组的计算命令")
    surfaces = array.array("f", controller.GetBufferData(surface_id, 0, 0))
    sample = [(i % width, i // width) for i, valid in enumerate(surfaces[3::12]) if valid == 1]
    xs, ys = zip(*sample)
    x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
    results = []
    for u, v, g, b in [(0.25, 0.25, 64, 255), (0.75, 0.25, 128, 0),
                       (0.25, 0.75, 192, 64), (0.75, 0.75, 255, 128)]:
        x = x0 + int((x1 - x0 + 1) * u)
        y = y0 + int((y1 - y0 + 1) * v)
        i = y * width + x
        surface = surfaces[12*i:12*i+12]
        roughness, metallic = surface[7], surface[11]
        assert math.dist(surface[4:7], (0, 0, 1)) < 1e-4
        assert abs(roughness - .8 * g / 255) < .00014 and abs(metallic - .6 * b / 255) < .00014
        results.append(dict(x=x, y=y, source_G=g, source_B=b, roughness=roughness, metallic=metallic,
                            linear_rgb=list(surface[8:11]), normal=list(surface[4:7]), packed_alpha=albedo[4*i+3]))

    gray = lambda data: bytes(round(255 * min(1., max(0., x))) for x in data)
    save_png(out/'roughness.png', width, height, gray(surfaces[7::12]), 1)
    save_png(out/'metallic.png', width, height, gray(surfaces[11::12]), 1)
    # 底色图存线性 RGB。给通常的 sRGB 显示器看时，把线性亮度转成像素字节。
    rgb = gray(12.92 * c if c <= .0031308 else 1.055 * c ** (1/2.4) - .055
               for i, c in enumerate(albedo) if i % 4 != 3)
    save_png(out/'albedo.png', width, height, rgb, 3)
    controller.SetFrameEvent(dispatches[-1].eventId, True)
    bindings = controller.GetPipelineState().GetReadWriteResources(rd.ShaderStage.Compute)
    display = next(textures[d.descriptor.resource] for d in bindings if d.descriptor.resource in textures)
    result = array.array("f", controller.GetTextureData(display.resourceId, rd.Subresource(0, 0, 0)))
    save_png(out/'panel.png', display.width, display.height, gray(c for i,c in enumerate(result) if i % 4 != 3), 3)
    return dict(capture=filename, tool=rd.GetVersionString(), frame=frame.frameNumber, size=[width,height],
                draw_event=draw.eventId, pack_event=event.eventId, display_event=dispatches[-1].eventId,
                draw_targets=[str(i) for i in outputs], surface_buffer=str(surface_id), covered=len(sample),
                debug_messages=[m.description for m in frame.debugMessages], samples=results)


def main():
    path = Path(os.environ.get("RF_MATERIAL_CAPTURE", "output/captures/local/MaterialChannels-frame12.rdc"))
    out = Path(os.environ.get("RF_MATERIAL_OUTPUT", "output/profiling/material-channels"))
    out.mkdir(parents=True, exist_ok=True)
    capture = rd.OpenCaptureFile()
    controller = None
    try:
        assert capture.OpenFile(str(path), "", None) == rd.ResultCode.Succeeded
        status, controller = capture.OpenCapture(rd.ReplayOptions(), None)
        assert status == rd.ResultCode.Succeeded
        result = export(controller, out, controller.GetFrameInfo(), path.name)
        (out/"samples.json").write_text(json.dumps(result, indent=2), encoding="utf8")
    finally:
        if controller:
            controller.Shutdown()
        capture.Shutdown()


try:
    main()
except Exception:
    traceback.print_exc()
    sys.exit(1)
sys.exit(0)
