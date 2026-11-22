"""在项目根目录用 qrenderdoc --python tests/replay_gi_mask.py 核对 GPU 截帧和实时读数。"""
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import traceback

import renderdoc as rd

out = Path("output/validation/gi-mask")
work = Path("bin-int/Tests/gi-mask")
fine = os.environ.get("RF_FINE_MASK_CAPTURE")
if fine:
    assert fine in ("before", "after", "far-before", "far-after")
    out = out.with_name("fine-mask")
    work = Path("tmp/fine-mask") / fine / "wires" / work
capture = rd.OpenCaptureFile()
controller = None


def leaves(actions):
    for action in actions:
        if action.flags & (rd.ActionFlags.Dispatch | rd.ActionFlags.Drawcall):
            yield action
        yield from leaves(action.children)


def summarize(resource, count, kind):
    data = bytes(controller.GetBufferData(resource, 0, count))
    stride = 12 if kind == "position" else 4
    pixels = {}
    points = ((26,32), (33,29), (6,29)) if fine else ((33,29), (18,29), (6,29))
    for x, y in points:
        pixels[f"{x},{y}"] = struct.unpack_from("<4f", data, stride * 4 * (y * 64 + x))
    return data, dict(id=str(resource), bytes=len(data), sha256=hashlib.sha256(data).hexdigest(),
                      sample_xyzw=pixels)


def cutout_inputs(slots):
    # 测试只有一块白墙和一块布，各由两个三角形组成。记录裁剪值和 GPU 真正读到的 RGBA，不根据文件名推断。
    triangles = bytes(controller.GetBufferData(slots[1], 0, 4 * 64))
    mask_faces = [int(-v[15]) - 1 for v in struct.iter_unpack("<16f", triangles) if v[15] < 0]
    assert len(mask_faces) == 2
    face = bytes(controller.GetBufferData(slots[11], mask_faces[0] * 64, 64))
    mask_index = int(struct.unpack_from("<f", face, 28)[0])
    offset, width, height, detail = struct.unpack_from("<4I", face, 32)
    settings = bytes(controller.GetBufferData(slots[14], 32 * mask_index, 32))
    alpha, cutoff = struct.unpack_from("<2f", settings)
    assert (alpha, cutoff) == (1., .5)
    texture = bytes(controller.GetBufferData(slots[12], 4 * offset, width * height * 4))
    texels = list(struct.iter_unpack("<4B", texture))
    transparent = sum(t[3] == 0 for t in texels)
    report = dict(triangles=4, masked_triangles=len(mask_faces), mask_face_indices=mask_faces,
                alpha_factor=alpha, cutoff=cutoff, texture_dimensions=[width,height],
                transparent_texels=transparent, corner_texel_rgba8=texels[0],
                center_texel_rgba8=texels[(height // 2) * width + width // 2])
    if not fine:
        assert width == height == 16 and transparent == 36
        return report
    assert (width,height) == (512,16) and transparent == width * height
    if fine.endswith("before"):
        assert detail == 0
    else:
        assert detail > 0
        # 渲染用的底色只保留 512 列；射线额外读取原图透明度，每个 uint32 放四列的值。
        start = (detail - 1) * 4
        size = struct.unpack("<2I", bytes(controller.GetBufferData(slots[12], start, 8)))
        assert size == (1024,32)
        pixels = bytes(controller.GetBufferData(slots[12], start + 8, size[0] * size[1]))
        assert pixels == bytes([255,0,0,0]) * (len(pixels) // 4)
        report["full_resolution_alpha"] = dict(size=size, bytes=len(pixels), opaque_texels=pixels.count(255),
                                              first_eight=list(pixels[:8]))
    return report


def raster_texture(state):
    textures = {texture.resourceId: texture for texture in controller.GetTextures()}
    for use in state.GetReadOnlyResources(rd.ShaderStage.Pixel):
        texture = textures[use.descriptor.resource]
        if texture.width != 1024: continue
        sampler = use.sampler
        levels = []
        for mip in range(texture.mips):
            raw = bytes(controller.GetTextureData(texture.resourceId, rd.Subresource(mip,0,0)))
            alpha = raw[3::4]
            levels.append(dict(level=mip, min_alpha8=min(alpha), max_alpha8=max(alpha),
                               fraction_above_cutoff=sum(value >= 128 for value in alpha)/len(alpha)))
        return dict(resource=str(texture.resourceId), dimensions=[texture.width,texture.height],
                    format=texture.format.Name(), minify=str(sampler.filter.minify), magnify=str(sampler.filter.magnify),
                    mip_filter=str(sampler.filter.mip), lod_bias=sampler.mipBias, min_lod=sampler.minLOD,
                    max_lod=sampler.maxLOD, alpha_by_level=levels)


try:
    path = max(out.glob((fine or "Mask") + "*.rdc"), key=lambda f: f.stat().st_mtime_ns)
    assert capture.OpenFile(str(path), "", None) == rd.ResultCode.Succeeded
    status, controller = capture.OpenCapture(rd.ReplayOptions(), None)
    assert status == rd.ResultCode.Succeeded
    events = []
    compute = rd.ShaderStage.Compute
    pixel = rd.ShaderStage.Pixel
    passes = ["nearest_surface", "pack_visible_surfaces", "pack_receiver_alone", "all_point_shadows"]
    count = 0
    draws = 0
    scene_buffers = {}
    for action in leaves(controller.GetRootActions()):
        controller.SetFrameEvent(action.eventId, True)
        state = controller.GetPipelineState()
        event = dict(id=action.eventId)
        if action.flags & rd.ActionFlags.Dispatch:
            stage = passes[count]
            count += 1
            reflection = state.GetShaderReflection(compute)
            slots = {reflection.readWriteResources[r.access.index].fixedBindNumber: r.descriptor.resource
                     for r in state.GetReadWriteResources(compute)}
            event.update(pass_name=stage, groups=list(action.dispatchDimension),
                         shader=str(state.GetShader(compute)), bindings={str(k): str(v) for k,v in slots.items()})
            if stage in ("nearest_surface", "pack_visible_surfaces"):
                data, values = summarize(slots[2], 64*64*48, "position")
                live = work / ("mask-ray.bin" if stage == "nearest_surface" else "mask-raster.bin")
            elif stage == "all_point_shadows":
                data, values = summarize(slots[8], 64*64*16, "color")
                live = work / "mask-shadow.bin"
            else:
                data, values = summarize(slots[2], 64*64*48, "position")
                live = None
                assert all(abs(value[2]) < .0001 for value in values["sample_xyzw"].values())
            if live:
                assert live.read_bytes() == data, (stage, values)
                values["equal_to_live_readback"] = True
            event["output"] = values
            if stage == "all_point_shadows":
                for key in (1, 11, 12, 14):
                    assert slots[key] == scene_buffers[key]
                if fine and fine.startswith("far-"):
                    pass  # 远景记录单条射线和像素覆盖率的差异，读回已与对应帧逐字节核对。
                elif fine:
                    assert values["sample_xyzw"]["33,29"][0] > .25
                    assert (values["sample_xyzw"]["26,32"][0] > 0) == (fine == "before")
                else:
                    assert values["sample_xyzw"]["33,29"][0] > .25
                    assert values["sample_xyzw"]["18,29"][0] == 0
            if stage == "nearest_surface":
                scene_buffers = slots
                event["cutout_inputs"] = cutout_inputs(slots)
        else:
            draws += 1
            event.update(pass_name="rasterize", index_count=action.numIndices,
                         color_targets=[str(r.resource) for r in state.GetOutputTargets()],
                         fragment_shader=str(state.GetShader(pixel)),
                         sampled_textures=[str(r.descriptor.resource) for r in state.GetReadOnlyResources(pixel)])
            if fine:
                texture = raster_texture(state)
                if texture: event["base_texture"] = texture
        events.append(event)
    messages = [str(m.description) for m in controller.GetFrameInfo().debugMessages]
    assert count == 4 and draws == 3, (count, draws)
    assert not messages, messages
    if fine:
        tex, = [e["base_texture"] for e in events if "base_texture" in e]
        assert tex["alpha_by_level"][0]["fraction_above_cutoff"] == .25
        assert tex["alpha_by_level"][4]["fraction_above_cutoff"] == 0
    result = dict(capture=path.as_posix(), sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                  renderdoc=rd.GetVersionString(), api=str(controller.GetAPIProperties().pipelineType),
                  draws=draws, dispatches=count, messages=messages, events=events)
except Exception:
    result = dict(error=traceback.format_exc())
finally:
    if controller:
        controller.Shutdown()
    capture.Shutdown()
    name = fine + "-replay.json" if fine else "replay.json"
    (out / name).write_text(json.dumps(result, indent=2) + "\n", encoding="utf8")
sys.exit(1 if "error" in result else 0)
