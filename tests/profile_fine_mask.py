"""从项目根目录运行。两个参数是同一机位、相同设置的旧版和新版截帧工作目录。

python tests/profile_fine_mask.py tmp/material-reference/Sponza-Mask-532eca6d7921-384 tmp/material-reference/Sponza-Mask-新版本-384
"""
import hashlib
import json
import os
from pathlib import Path
from statistics import median
import struct
import sys
import traceback


ROOT = Path.cwd()
OUT = ROOT / "output/profiling/fine-mask"
WORK = ROOT / "tmp/fine-mask-timing"
STORAGE = {5: "nodes", 6: "triangles", 7: "lights", 8: "scene", 9: "faces",
           10: "material_pixels", 11: "emissions", 12: "probes", 13: "masks"}


def digest(path):
    with path.open("rb") as stream:
        h = hashlib.sha256()
        for data in iter(lambda: stream.read(8 * 1024 * 1024), b""):
            h.update(data)
        return h.hexdigest()


def actions(items):
    for item in items:
        yield item
        yield from actions(item.children)


def inspect(rd, frame, work, export):
    result = {"stages": {}, "buffers": {}}
    calls = list(actions(frame.GetRootActions()))
    dispatches = [c for c in calls if c.flags & rd.ActionFlags.Dispatch]
    info = frame.GetFrameInfo()
    assert info.frameNumber == 24 and not info.debugMessages
    result.update(frame=info.frameNumber, draws=sum(bool(c.flags & rd.ActionFlags.Drawcall) for c in calls),
                  dispatches=len(dispatches), messages=len(info.debugMessages))
    submits = [c.metadata.durationMicro for c in frame.GetStructuredFile().chunks if c.name == "vkQueueSubmit"]
    result.update(queue_submit_count=len(submits), queue_submit_cpu_us=submits)
    buffers = {b.resourceId: b.length for b in frame.GetBuffers()}
    for call in dispatches:
        frame.SetFrameEvent(call.eventId, True)
        state = frame.GetPipelineState()
        shader = state.GetShaderReflection(rd.ShaderStage.Compute)
        threads = tuple(shader.dispatchThreadsDimension)
        constants = [b.byteSize for b in shader.constantBlocks]
        slots = {shader.readWriteResources[r.access.index].fixedBindNumber: r.descriptor.resource
                 for r in state.GetReadWriteResources(rd.ShaderStage.Compute)}
        push = bytes(frame.GetVulkanPipelineState().pushconsts)
        count = len(slots)
        if threads == (64, 1, 1) and constants == [20] and count == 15:
            mode = struct.unpack_from("<I", push)[0]
            name = {9: "probe_update", 10: "probe_shade", 11: "copy_previous_surface"}[mode]
        elif threads == (8, 8, 1) and not constants and count == 10:
            name = "cluster"
        elif threads == (8, 8, 1) and constants == [20] and count == 14:
            assert struct.unpack_from("<II", push) == (1, 1)
            name = "reflection"
            assert len({slots[s] for s in STORAGE}) == len(STORAGE)
            for slot, field in STORAGE.items():
                result["buffers"][field] = {"slot": slot, "bytes": buffers[slots[slot]]}
                if export and slot in (6, 9, 10, 13):
                    (work / (field + ".bin")).write_bytes(frame.GetBufferData(slots[slot], 0, 0))
        elif threads == (64, 1, 1) and constants == [20] and count == 10:
            assert struct.unpack_from("<i", push, 16)[0] == 8
            name = "fog"
        else:
            continue
        assert name not in result["stages"], name
        result["stages"][name] = dict(event=call.eventId, groups=list(call.dispatchDimension), gpu_ms=[])
    assert set(result["stages"]) == {"probe_update", "probe_shade", "copy_previous_surface", "cluster", "reflection", "fog"}, result
    return result


def replay(rd, cases):
    # 轮流回放两个版本，让先测或后测对时间的影响分摊到两组。首次访问时导出显存里的原始字节。
    results = {}
    counter = rd.GPUCounter.EventGPUDuration
    for name in ("old", "new", "new", "old"):
        item = cases[name]
        path = Path(item["capture"])
        cap = rd.OpenCaptureFile()
        frame = None
        try:
            assert cap.OpenFile(str(path), "", None) == rd.ResultCode.Succeeded
            status, frame = cap.OpenCapture(rd.ReplayOptions(), None)
            assert status == rd.ResultCode.Succeeded
            if name not in results:
                folder = WORK / name
                folder.mkdir(parents=True, exist_ok=True)
                results[name] = inspect(rd, frame, folder, True)
            assert counter in frame.EnumerateCounters()
            frame.FetchCounters([counter])
            for _ in range(6):
                times = {v.eventId: v.value.d * 1000. for v in frame.FetchCounters([counter])}
                for stage in results[name]["stages"].values():
                    stage["gpu_ms"].append(times[stage["event"]])
        finally:
            if frame:
                frame.Shutdown()
            cap.Shutdown()
    for name, data in results.items():
        for s in data["stages"].values():
            s["median_ms"] = median(s["gpu_ms"])
        cases[name].update(data)
    return dict(tool=rd.GetVersionString(), build="Debug x64", cases=cases,
                method="EventGPUDuration, milliseconds. ABBA; each visit discards one replay, then measures six.")


def metadata(work):
    import yaml
    scene = work / "assets/scenes/Sponza-Atrium.redflag"
    settings = Path(str(scene) + ".render.yaml")
    path = Path((work / "frame-path.txt").read_text(encoding="utf8")).resolve(strict=True)
    gpu = next(s.split("Vulkan device: ", 1)[1] for s in (work / "editor.log").read_text(
        encoding="utf8", errors="replace").splitlines() if "Vulkan device: " in s)
    return dict(capture=path.relative_to(ROOT).as_posix(), sha256=digest(path), scene_sha256=digest(scene),
                executable_sha_prefix=work.name.split("-")[-2], gpu=gpu, settings_sha256=digest(settings),
                settings=yaml.safe_load(settings.read_bytes()), size=json.loads((work / "replay.json").read_bytes())["size"])


def verify_final(exe, previous, renderdoc):
    from capture_material_studio import run_renderdoc
    stamp = digest(exe)
    directory = ROOT / "tmp/material-reference" / ("Sponza-Mask-" + stamp[:12] + "-384")
    final = metadata(directory)
    previous_metadata = metadata(previous)
    assert all(final[k] == previous_metadata[k] for k in ("scene_sha256", "settings_sha256", "size", "gpu"))
    folder = WORK / "final"
    folder.mkdir(parents=True, exist_ok=True)
    exported = folder / "capture.txt"
    exported.unlink(missing_ok=True)
    run_renderdoc(renderdoc, Path(__file__).resolve(), ROOT, dict(os.environ, RF_FINE_MASK_FINAL=final["capture"]))
    assert exported.read_text(encoding="utf8") == final["capture"]
    files = {}
    for name in ("triangles.bin", "faces.bin", "masks.bin", "material_pixels.bin", "surface.f32", "light.f32", "display.f32"):
        a, b = (WORK / "new", folder) if name.endswith(".bin") else (previous, directory)
        files[name] = {"timed_sha256": digest(a / name), "final_sha256": digest(b / name)}
        assert files[name]["timed_sha256"] == files[name]["final_sha256"], name
    data = dict(timed=previous_metadata, final=final, final_executable_sha256=stamp, files=files,
                scope="Fresh frame 24. Compared captured storage and float outputs byte-for-byte. No timings from this run.")
    (OUT / "final-frame.json").write_text(json.dumps(data, indent=2) + "\n", encoding="utf8")


def compare_display(old, new):
    import numpy as np
    from PIL import Image, ImageDraw, ImageFont
    views = []
    for case in (old, new):
        rgb = np.fromfile(case / "display.f32", "<f4").reshape(246, 384, 4)[:, :, :3]
        views.append(np.uint8(np.clip(rgb, 0, 1) * 255 + .5))
    difference = np.abs(views[0].astype(int) - views[1]).max(axis=2)
    overlay = np.repeat((views[1].mean(axis=2, keepdims=True) * .30).astype(np.uint8), 3, axis=2)
    for step, color in ((1, [126, 204, 210]), (2, [244, 191, 90]), (8, [237, 86, 102])):
        overlay[difference >= step] = color
    sheet = Image.new("RGB", (384 * 3 + 12, 246 + 40), "white")
    draw = ImageDraw.Draw(sheet)
    font = ImageFont.truetype("msyh.ttc", 16)
    for i, (img, label) in enumerate(zip([*views, overlay], ["旧版", "新版", "差值　青 ≥1　黄 ≥2　红 ≥8 级"])):
        sheet.paste(Image.fromarray(img), (390*i, 0))
        draw.text((390*i + 4, 252), label, fill="#283638", font=font)
    sheet.save(OUT / "sponza-frame.png")
    report = dict(size=[384, 246], frame=24, measure="Round each clamped display RGB channel to 8 bits. "
                  "For each pixel take largest absolute old/new channel difference before forming counts.",
                  max_change=int(difference.max()), total_pixels=int(difference.size),
                  pixels_changed={str(k): int((difference >= k).sum()) for k in (1, 2, 8)},
                  old_display_sha256=digest(old / "display.f32"), new_display_sha256=digest(new / "display.f32"))
    (OUT / "sponza-frame.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf8")


def run():
    import argparse
    from capture_material_studio import run_renderdoc
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("old", type=Path)
    parser.add_argument("new", type=Path)
    parser.add_argument("--renderdoc", type=Path, default=Path("tmp/tools/renderdoc/RenderDoc_1.47_64"))
    parser.add_argument("--verify-final", type=Path, help="用已经捕获的最终版，比对计时版本的材料和画面")
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    WORK.mkdir(parents=True, exist_ok=True)
    if args.verify_final:
        verify_final(args.verify_final, args.new, args.renderdoc.resolve())
        return
    cases = {key: metadata(getattr(args, key)) for key in ("old", "new")}
    assert all(cases["old"][key] == cases["new"][key] for key in ("scene_sha256", "settings_sha256", "gpu", "size"))
    assert cases["new"]["size"] == [384, 246]
    done = WORK / "replay-result.json"
    done.unlink(missing_ok=True)
    run_renderdoc(args.renderdoc.resolve(), Path(__file__).resolve(), ROOT,
                  dict(os.environ, RF_FINE_MASK_TIMES=json.dumps(cases)))
    result = json.loads(done.read_bytes())
    for stage in result["cases"]["old"]["stages"]:
        assert result["cases"]["old"]["stages"][stage]["groups"] == result["cases"]["new"]["stages"][stage]["groups"]
    (OUT / "gpu.json").write_bytes(done.read_bytes())
    from sample_fine_masks import compare
    compare(ROOT, WORK, OUT)
    compare_display(args.old, args.new)


if "RF_FINE_MASK_FINAL" in os.environ:
    import renderdoc as rd
    cap = rd.OpenCaptureFile()
    frame = None
    try:
        assert cap.OpenFile(os.environ["RF_FINE_MASK_FINAL"], "", None) == rd.ResultCode.Succeeded
        status, frame = cap.OpenCapture(rd.ReplayOptions(), None)
        assert status == rd.ResultCode.Succeeded
        inspect(rd, frame, WORK / "final", True)
        (WORK / "final/capture.txt").write_text(os.environ["RF_FINE_MASK_FINAL"], encoding="utf8")
    finally:
        if frame:
            frame.Shutdown()
        cap.Shutdown()
    sys.exit(0)
elif "RF_FINE_MASK_TIMES" in os.environ:
    import renderdoc as rd
    try:
        data = replay(rd, json.loads(os.environ["RF_FINE_MASK_TIMES"]))
        (WORK / "replay-result.json").write_text(json.dumps(data, indent=2) + "\n", encoding="utf8")
    except Exception:
        (WORK / "error.txt").write_text(traceback.format_exc(), encoding="utf8")
        sys.exit(1)
    sys.exit(0)
elif __name__ == "__main__":
    run()
