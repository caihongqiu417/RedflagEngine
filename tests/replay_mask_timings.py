"""从仓库根目录回放两份 Sponza 第 24 帧，分别测每条命令的 GPU 耗时。

python tests/replay_mask_timings.py tmp/probe-materials/Forge-72bcd0a87419.exe tmp/gi-mask-sponza/Forge-532eca6d7921.exe

如需重新截帧，在同一命令末尾加 --capture。两个位置参数依次为修复前、修复后的 Debug x64 编辑器。
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
from statistics import median
import struct
import subprocess
import sys
import traceback


ROOT = Path.cwd()
OUTPUT = ROOT / "output/profiling/gi-mask/sponza-timing.json"


def sha(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(8 * 1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def walk(actions):
    for action in actions:
        yield action
        yield from walk(action.children)


def measure(rd, frame, masked):
    calls = list(walk(frame.GetRootActions()))
    dispatches = [a for a in calls if a.flags & rd.ActionFlags.Dispatch]
    stages = {}
    for action in dispatches:
        frame.SetFrameEvent(action.eventId, True)
        state = frame.GetPipelineState()
        shader = state.GetShaderReflection(rd.ShaderStage.Compute)
        threads = tuple(shader.dispatchThreadsDimension)
        constants = [b.byteSize for b in shader.constantBlocks]
        resources = {r.fixedBindNumber: r.variableType for r in shader.readWriteResources}
        push = bytes(frame.GetVulkanPipelineState().pushconsts)
        count = len(resources)

        if threads == (64, 1, 1) and constants == [20] and count >= 14 and struct.unpack_from("<I", push)[0] == 9:
            name = "probe"
        elif threads == (8, 8, 1) and not constants and count == (10 if masked else 7):
            if resources[5].members[0].type.arrayByteStride != 48 or resources[6].members[0].type.arrayByteStride != 64:
                continue
            name = "cluster"
        elif threads == (8, 8, 1) and constants == [20] and count == (14 if masked else 13):
            assert struct.unpack_from("<II", push) == (1, 1)
            name = "reflection"
        elif threads == (64, 1, 1) and constants == [20] and count == (10 if masked else 7):
            assert struct.unpack_from("<i", push, 16)[0] == 8
            name = "fog"
        else:
            continue
        assert name not in stages
        stages[name] = dict(event=action.eventId, groups=list(action.dispatchDimension), bindings=count)

    assert set(stages) == {"probe", "cluster", "reflection", "fog"}, list(stages)
    info = frame.GetFrameInfo()
    assert info.frameNumber == 24 and not info.debugMessages
    counter = rd.GPUCounter.EventGPUDuration
    assert counter in frame.EnumerateCounters()
    frame.FetchCounters([counter])
    samples = [{v.eventId: v.value.d * 1000 for v in frame.FetchCounters([counter])} for _ in range(11)]
    for item in stages.values():
        item["gpu_ms"] = [row[item["event"]] for row in samples]
        item["median_ms"] = median(item["gpu_ms"])
    return dict(frame=info.frameNumber, draws=sum(1 for a in calls if a.flags & rd.ActionFlags.Drawcall),
                dispatches=len(dispatches), stages=stages)


def replay(rd, path, masked):
    capture = rd.OpenCaptureFile()
    frame = None
    try:
        assert capture.OpenFile(str(path), "", None) == rd.ResultCode.Succeeded
        status, frame = capture.OpenCapture(rd.ReplayOptions(), None)
        assert status == rd.ResultCode.Succeeded
        result = measure(rd, frame, masked)
    finally:
        if frame:
            frame.Shutdown()
        capture.Shutdown()
    result.update(capture=path.relative_to(ROOT).as_posix(), capture_sha256=sha(path))
    return result


def run():
    import numpy as np
    from PIL import Image
    import yaml
    from capture_material_studio import run_renderdoc
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before", type=Path)
    parser.add_argument("after", type=Path)
    parser.add_argument("--capture", action="store_true", help="用两份编辑器重新截取同一机位的第 24 帧")
    parser.add_argument("--renderdoc", type=Path, default=Path("tmp/tools/renderdoc/RenderDoc_1.47_64"))
    args = parser.parse_args()
    result = {}
    if args.capture:
        source = ROOT / "tmp/gi-mask-timing/Sponza-Mask.redflag"
        source.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / "RedflagForge/assets/scenes/Sponza-Atrium.redflag", source)
        settings = dict(Shading="Lit", Direct="Clustered lights", Indirect="DDGI", GTAO=True, SSR=True,
                        SceneReflections=True, TAA=True, HistoryWeight=.85, Samples=2, VolumetricFog=True,
                        FogDensity=.06, FogSteps=8, PreviewWidth=384)
        Path(str(source) + ".render.yaml").write_text(yaml.safe_dump(settings), encoding="utf8")
    for mode, exe in (("before", args.before), ("after", args.after)):
        if args.capture:
            subprocess.run([sys.executable, "tests/capture_material_studio.py", "--exe", exe, "--renderdoc", args.renderdoc,
                            "--scene", source, "--width", "384"], cwd=ROOT, check=True)
        stamp = sha(exe)
        name = "Sponza-Mask-" + stamp[:12] + "-384"
        work = ROOT / "tmp/material-reference" / name
        scene = work / "assets/scenes/Sponza-Atrium.redflag"
        settings = Path(str(scene) + ".render.yaml")
        info = json.loads((work / "replay.json").read_bytes())
        path = Path((work / "frame-path.txt").read_text(encoding="utf8")).resolve(strict=True)
        device = next(line.split("Vulkan device: ", 1)[1] for line in (work / "editor.log").read_text(
            encoding="utf8", errors="replace").splitlines() if "Vulkan device: " in line)
        result[mode] = dict(executable_sha256=stamp, scene_sha256=sha(scene), settings_sha256=sha(settings),
                            settings=yaml.safe_load(settings.read_bytes()), size=info["size"], path=str(path), device=device)
    assert all(result["before"][k] == result["after"][k] for k in ("settings_sha256", "scene_sha256", "size", "device"))
    assert result["after"]["size"] == [384, 246]
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    run_renderdoc(args.renderdoc.resolve(), Path(__file__).resolve(), ROOT,
                  dict(os.environ, RF_MASK_TIMINGS=json.dumps(result)))
    width, height = result["after"]["size"]
    view = np.fromfile(work / "display.f32", "<f4").reshape(height, width, 4)
    Image.fromarray(np.uint8(np.clip(view[:, :, :3], 0, 1) * 255 + .5)).save(OUTPUT.with_name("sponza-timing-view.png"))


if "RF_MASK_TIMINGS" in os.environ:
    import renderdoc as rd
    try:
        cases = json.loads(os.environ["RF_MASK_TIMINGS"])
        for name, entry in cases.items():
            entry.update(replay(rd, Path(entry.pop("path")), name == "after"))
        for name in cases["after"]["stages"]:
            assert cases["before"]["stages"][name]["groups"] == cases["after"]["stages"][name]["groups"]
        report = dict(scene="Sponza", device=cases["after"]["device"], build="Debug x64",
                      tool=rd.GetVersionString(), statistic="EventGPUDuration (ms); 1 warm-up, 11 measured replays per capture",
                      cases=cases)
        OUTPUT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf8")
    except Exception:
        (ROOT / "tmp/gi-mask-sponza/timing-error.txt").write_text(traceback.format_exc(), encoding="utf8")
        sys.exit(1)
    sys.exit(0)
else:
    run()
