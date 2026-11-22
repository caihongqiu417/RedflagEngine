"""Set RF_STUDIO_CAPTURE to an .rdc file and run qrenderdoc --python tests/replay_material_studio.py."""
import json
import os
from pathlib import Path
from statistics import median
import struct
import sys
import traceback

import renderdoc as rd


def walk(actions):
    for action in actions:
        yield action
        yield from walk(action.children)


def measure_reflections(frame, dispatches, directory, pixels):
    buffers = {b.resourceId for b in frame.GetBuffers()}
    def compute_pass(event):
        frame.SetFrameEvent(event, True)
        state = frame.GetPipelineState()
        shader = state.GetShaderReflection(rd.ShaderStage.Compute)
        slots = {shader.readWriteResources[r.access.index].fixedBindNumber: r.descriptor.resource
                 for r in state.GetReadWriteResources(rd.ShaderStage.Compute)}
        return slots, tuple(shader.dispatchThreadsDimension), [b.byteSize for b in shader.constantBlocks]

    trace = filtered = None
    consumed = False
    for event in dispatches:
        slots, threads, constants = compute_pass(event)
        if trace:
            if (threads == (8, 8, 1) and len(slots) == 5 and constants == [4]
                    and slots.get(2) == inputs[2] and slots.get(3) == inputs[3]):
                filtered, final = event, slots[4]
                continue
            # 开 TAA 时它会接过反射图。也可能是雾、透明物体或最后把颜色写进面板的着色器。
            if final in slots.values():
                consumed = True
                break
            continue
        if threads != (8, 8, 1) or not set(slots.values()) <= buffers:
            continue
        if len(slots) not in (5, 13, 14) or set(slots) != set(range(len(slots))):
            continue
        option_size = 12 if len(slots) == 5 else 20
        if constants != [option_size]:
            continue
        push = bytes(frame.GetVulkanPipelineState().pushconsts)
        material, trace_screen, sky = struct.unpack_from("<3I", push)
        if material == 1 and trace_screen == 1:
            trace, inputs, final = event, slots, slots[3]
            options = push[:option_size]
    if not consumed:
        raise ValueError("Could not follow the material reflection output into the next editor pass")

    frame.SetFrameEvent(trace, True)
    (directory / "reflection-in.f32").write_bytes(frame.GetBufferData(inputs[2], 0, pixels * 16))
    (directory / "reflection-out.f32").write_bytes(frame.GetBufferData(inputs[3], 0, pixels * 16))
    if os.environ.get("RF_EXPORT_REFLECTION_SCENE") == "1":
        folder = directory / "reflection-buffers"
        folder.mkdir(exist_ok=True)
        if len(inputs) not in (13, 14):
            raise ValueError("The full-scene reference needs the material and triangle buffers")
        for binding, resource in inputs.items():
            (folder / (str(binding) + ".bin")).write_bytes(frame.GetBufferData(resource, 0, 0))
        (folder / "options.bin").write_bytes(options)
        (folder / "binding-count.txt").write_text(str(len(inputs)), encoding="ascii")
    if filtered:
        frame.SetFrameEvent(filtered, True)
        (directory / "reflection-filtered.f32").write_bytes(frame.GetBufferData(final, 0, pixels * 16))

    counter = rd.GPUCounter.EventGPUDuration
    if counter not in frame.EnumerateCounters():
        raise RuntimeError("GPU duration counter is unavailable on the replay device")
    frame.FetchCounters([counter])
    timings = {trace: []}
    if filtered:
        timings[filtered] = []
    for _ in range(11):
        values = {v.eventId: v.value.d * 1000 for v in frame.FetchCounters([counter])}
        for event, series in timings.items():
            series.append(values[event])
    info = dict(event=trace, scene_trace=len(inputs) >= 13, environment=bool(struct.unpack_from("<3I", options)[2]),
                gpu_ms=timings[trace], median_gpu_ms=median(timings[trace]))
    if len(inputs) == 14:
        info["alpha_mask"] = True
    if filtered:
        info["filter"] = dict(event=filtered, gpu_ms=timings[filtered], median_gpu_ms=median(timings[filtered]))
    return info


def export(frame, directory):
    textures = {texture.resourceId: texture for texture in frame.GetTextures()}
    calls = list(walk(frame.GetRootActions()))
    dispatches = [a.eventId for a in calls if a.flags & rd.ActionFlags.Dispatch]
    draws = [a.eventId for a in calls if a.flags & rd.ActionFlags.Drawcall]
    if not dispatches:
        raise ValueError("This frame only redisplayed the previous image. Capture while the scene is drawing.")

    # 编辑器最后一次计算把线性光照转成面板颜色。在同一个事件读回转换前后的两份数据。
    frame.SetFrameEvent(dispatches[-1], True)
    display = frame.GetPipelineState()
    resources = display.GetReadWriteResources(rd.ShaderStage.Compute)
    target = next(textures[r.descriptor.resource] for r in resources if r.descriptor.resource in textures)
    # 最早版本只有灰度开关，后续版本加了每边的样点数，再加入显示曲线与曝光倍数。
    options = display.GetShaderReflection(rd.ShaderStage.Compute).constantBlocks[0].byteSize
    push = bytes(frame.GetVulkanPipelineState().pushconsts)
    assert options in (4, 8, 16), f"Unexpected panel display parameters: {options} bytes"
    side = struct.unpack_from("<I", push, 4)[0] if options >= 8 else 1
    filmic, exposure = struct.unpack_from("<If", push, 8) if options == 16 else (0, 1.0)
    assert side in (1, 2)
    width, height = target.width * side, target.height * side
    pixels = width * height
    source = next(r.descriptor.resource for r in resources if r.descriptor.resource not in textures)
    (directory / "display.f32").write_bytes(frame.GetTextureData(target.resourceId, rd.Subresource()))
    (directory / "light.f32").write_bytes(frame.GetBufferData(source, 0, pixels * 16))

    # 打包表面的计算读取同尺寸的底色与法线深度图。保留颜色的第四项，供对照粗糙度和金属度。
    pack = None
    for event in dispatches:
        frame.SetFrameEvent(event, True)
        slots = frame.GetPipelineState().GetReadWriteResources(rd.ShaderStage.Compute)
        inputs = [textures[s.descriptor.resource] for s in slots if s.descriptor.resource in textures]
        if len(inputs) == 2 and all(t.width == width and t.height == height for t in inputs):
            (directory / "surface.f32").write_bytes(frame.GetTextureData(inputs[0].resourceId, rd.Subresource()))
            pack = event
            break
    report = {"frame": frame.GetFrameInfo().frameNumber, "size": [target.width, target.height],
            "shading_size": [width, height], "samples_per_pixel": side * side,
            "tone_mapping": "ACES" if filmic else "Reinhard", "exposure_multiplier": exposure,
            "dispatches": dispatches, "draws": draws, "pack_event": pack, "display_event": dispatches[-1],
            "messages": [m.description for m in frame.GetFrameInfo().debugMessages]}
    if os.environ.get("RF_EXPORT_TAA") == "1":
        for event in reversed(dispatches):
            frame.SetFrameEvent(event, True)
            state = frame.GetPipelineState()
            shader = state.GetShaderReflection(rd.ShaderStage.Compute)
            slots = {shader.readWriteResources[r.access.index].fixedBindNumber: r.descriptor.resource
                     for r in state.GetReadWriteResources(rd.ShaderStage.Compute)}
            # TAA 把颜色写到位置 5。显示着色器应当读取这同一份结果。
            if set(slots) == set(range(7)) and slots[5] == source:
                for binding, name in ((0, "taa-camera.bin"), (2, "taa-input.f32"), (6, "taa-position.f32")):
                    (directory / name).write_bytes(frame.GetBufferData(slots[binding], 0, 0))
                report["taa_event"] = event
                break
        if "taa_event" not in report:
            raise ValueError("Could not trace the panel colors to the TAA output")
    if os.environ.get("RF_MEASURE_REFLECTIONS") == "1":
        report["reflection"] = measure_reflections(frame, dispatches, directory, pixels)
    return report


path = Path(os.environ["RF_STUDIO_CAPTURE"]).resolve()
directory = Path(os.environ.get("RF_STUDIO_EXPORT", str(Path("tmp/material-reference") / path.stem)))
directory.mkdir(parents=True, exist_ok=True)
capture = rd.OpenCaptureFile()
controller = None
try:
    if capture.OpenFile(str(path), "", None) != rd.ResultCode.Succeeded:
        raise ValueError(f"Could not open {path}")
    status, controller = capture.OpenCapture(rd.ReplayOptions(), None)
    if status != rd.ResultCode.Succeeded:
        raise RuntimeError(str(status))
    result = export(controller, directory)
    result.update(file=path.name, tool=rd.GetVersionString())
except BaseException:
    result = {"error": traceback.format_exc()}
finally:
    if controller:
        controller.Shutdown()
    capture.Shutdown()
    (directory / "replay.json").write_text(json.dumps(result, indent=2), encoding="utf8")
sys.exit(1 if "error" in result else 0)
