"""测 RTXGI Bathroom 原生尺寸下的整帧时间，并区分重画场景和继续显示上一张图。"""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
from statistics import median
import subprocess


ROOT = Path.cwd() if os.environ.get("RF_REFLECTION_SUBMIT_WORK") else Path(__file__).resolve().parents[1]
OUT = ROOT / "output/profiling/rough-reflections"
WORK = ROOT / "tmp/rough-reflections/editor-timing"
MODES = ("default", "raw", "filtered")


def metrics(values):
    from statistics import quantiles

    return dict(samples=len(values), p50_ms=median(values), p95_ms=quantiles(values, n=20, method="inclusive")[18])


def frame_samples(events):
    from profile_native_navigation import MARKERS

    frames = sorted((e for e in events if e.get("name") == "RunLoop"), key=lambda e: e["ts"])
    markers = [e for e in events if e.get("name") in MARKERS]
    samples = []
    for frame in frames:
        inside = [e for e in markers if e["tid"] == frame["tid"] and frame["ts"] <= e["ts"]
                  and e["ts"] + e["dur"] <= frame["ts"] + frame["dur"]]
        if inside:
            assert len(inside) == 1
            samples.append((inside[0]["name"], frame["dur"] / 1000, inside[0]["dur"] / 1000))

    # 面板完成最后一次缩放后，32 帧使用同一个相机。跳过前 8 帧和关闭窗口的最后一帧。
    start = max(i for i, row in enumerate(samples) if row[0] == "Lighting camera motion")
    fixed = samples[start:]
    assert all(row[0] == "Lighting accumulation" for row in fixed[1:32])
    assert len(fixed) >= 50 and all(row[0] == "Lighting cached" for row in fixed[32:])
    return fixed[8:32], fixed[33:49]


def launch(exe, desktop, env, log):
    import msvcrt
    import win32con
    import win32event
    import win32process

    startup = win32process.STARTUPINFO()
    startup.lpDesktop = "WinSta0\\" + desktop
    startup.dwFlags = win32con.STARTF_USESTDHANDLES
    command = subprocess.list2cmdline([str(exe), "--vulkan", "--frames", "56"])
    with log.open("wb") as output, open(os.devnull, "rb") as null:
        startup.hStdOutput = startup.hStdError = msvcrt.get_osfhandle(output.fileno())
        startup.hStdInput = msvcrt.get_osfhandle(null.fileno())
        for handle in (startup.hStdOutput, startup.hStdInput):
            os.set_handle_inheritable(int(handle), True)
        process, thread, _, _ = win32process.CreateProcess(None, command, None, None, True,
            win32con.CREATE_NO_WINDOW, env, str(WORK), startup)
        try:
            if win32event.WaitForSingleObject(process, 180000) != win32con.WAIT_OBJECT_0:
                win32process.TerminateProcess(process, 1)
                raise TimeoutError(command)
            if win32process.GetExitCodeProcess(process):
                raise RuntimeError(f"编辑器运行失败 {log}")
        finally:
            thread.Close()
            process.Close()


def editor_times(exe):
    import win32con
    import win32service
    import yaml
    from profile_native_navigation import prepare

    prepare(WORK)
    scene = ROOT / "RedflagForge/assets/scenes/RTXGI-Bathroom.redflag"
    entry = WORK / "assets/scenes/Sponza-Atrium.redflag"
    shutil.copyfile(scene, entry)
    shutil.copyfile(str(scene) + ".rfprobes", str(entry) + ".rfprobes")
    settings = yaml.safe_load(Path(str(scene) + ".render.yaml").read_bytes())
    assert settings["PreviewWidth"] == 0 and not settings["SSR"] and settings["TAA"]
    binaries = ROOT / "bin/Debug-windows-x86_64/RedflagForge"
    env = dict(os.environ, PATH=str(binaries) + os.pathsep + os.environ["PATH"])
    for name in ("RF_VULKAN_VALIDATION", "VK_INSTANCE_LAYERS", "VK_ADD_LAYER_PATH", "ENABLE_VULKAN_RENDERDOC_CAPTURE"):
        env.pop(name, None)

    out = OUT / "editor-traces"
    out.mkdir(parents=True, exist_ok=True)
    runs = []
    size = None
    name = f"ReflectionTiming{os.getpid()}"
    desktop = win32service.CreateDesktop(name, 0, win32con.GENERIC_ALL, None)
    try:
        for cycle in range(3):
            for mode in MODES[cycle:] + MODES[:cycle]:
                config = dict(settings)
                if mode != "default":
                    config.update(SSR=True, SceneReflections=True, DenoiseReflections=mode == "filtered")
                Path(str(entry) + ".render.yaml").write_text(yaml.safe_dump(config), encoding="utf8")
                (WORK / "imgui-vulkan.ini").unlink(missing_ok=True)
                tag = f"{cycle + 1}-{mode}"
                log = WORK / (tag + ".log")
                launch(exe, name, env, log)
                output = log.read_bytes()
                pairs = re.findall(rb"Lighting preview (\d+) x (\d+), viewport (\d+) x (\d+)", output)
                w, h, vw, vh = map(int, pairs[-1])
                assert (w, h) == (vw, vh) and (size is None or size == [w, h])
                size = [w, h]
                gpu = re.search(rb"Vulkan device: ([^\r\n]+)", output)[1].decode()
                raw = (WORK / "RedflagProfile-Runtime.json").read_bytes()
                trace = out / (tag + ".json.gz")
                trace.write_bytes(gzip.compress(raw, mtime=0))
                drawn, cached = frame_samples(json.loads(raw)["traceEvents"])
                row = dict(cycle=cycle + 1, mode=mode, trace=trace.relative_to(ROOT).as_posix(),
                           redrawn_frame_ms=[v[1] for v in drawn], cached_frame_ms=[v[1] for v in cached],
                           lighting_scope_ms=[v[2] for v in drawn])
                runs.append(row)
                print(tag, size, "redrawn P50", median(row["redrawn_frame_ms"]), flush=True)
    finally:
        desktop.CloseDesktop()

    summary = {mode: {field: metrics([t for run in runs if run["mode"] == mode for t in run[field]])
                      for field in ("redrawn_frame_ms", "cached_frame_ms", "lighting_scope_ms")} for mode in MODES}
    report = dict(scene=scene.name, scene_sha256=hashlib.sha256(scene.read_bytes()).hexdigest(),
                  executable_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(), build="Debug x64", gpu=gpu,
                  size=size, renderdoc=False, vulkan_validation=False, default_settings=settings,
                  source="RunLoop main-thread wall time includes GPU waits, frame presentation and GUI. "
                         "Lighting accumulation is the elapsed time inside LightingPreview::Render after its cached return check.",
                  method="Three process launches per mode in rotating order, same motionless camera. After the last resize, "
                         "time rendered frames 9-32 and separately time cached frames 2-17; 56 editor frames per launch.",
                  limitations="Instrumented Debug build, fixed view on a separate Windows desktop, no mouse input. "
                              "Cached frames reuse the same color image. No GPU-only frame time or independent CPU occupancy measured.",
                  summary=summary, runs=runs)
    (OUT / "editor-timing.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf8")


def captured_submits(work):
    import renderdoc as rd

    path = Path((work / "frame-path.txt").read_text(encoding="utf8"))
    info = json.loads((work / "replay.json").read_bytes())
    stages = {"reflection": info["reflection"]["event"]}
    if "filter" in info["reflection"]:
        stages["filter"] = info["reflection"]["filter"]["event"]
    capture = rd.OpenCaptureFile()
    frame = None
    try:
        assert capture.OpenFile(str(path), "", None) == rd.ResultCode.Succeeded
        status, frame = capture.OpenCapture(rd.ReplayOptions(), None)
        assert status == rd.ResultCode.Succeeded
        structure = frame.GetStructuredFile()
        chunks = structure.chunks

        def visit(rows):
            for row in rows:
                yield row
                yield from visit(row.children)

        def field(obj, name):
            return next(obj.GetChild(i) for i in range(obj.NumChildren()) if obj.GetChild(i).name == name)

        submits = [(index, chunk) for index, chunk in enumerate(chunks) if chunk.name == "vkQueueSubmit"]
        details = {}
        for action in visit(frame.GetRootActions()):
            if action.eventId not in stages.values():
                continue
            stage = next(name for name, event in stages.items() if action.eventId == event)
            dispatch = action.events[-1].chunkIndex
            assert chunks[dispatch].name == "vkCmdDispatch"
            command = field(chunks[dispatch], "commandBuffer").data.basic.u
            # 后端反复用同一份命令缓冲区。按本次开始、结束和提交的位置核对，单看编号无法区分它们。
            begin = next(i for i in range(dispatch - 1, -1, -1) if chunks[i].name == "vkBeginCommandBuffer")
            submit, call = next((i, q) for i, q in submits if i > dispatch)
            end = next(i for i in range(dispatch + 1, submit) if chunks[i].name == "vkEndCommandBuffer")
            assert field(chunks[begin], "CommandBuffer").data.basic.u == command
            assert field(chunks[end], "CommandBuffer").data.basic.u == command
            names = [chunks[i].name for i in range(begin, submit)]
            assert names.count("vkCmdDispatch") == names.count("vkBeginCommandBuffer") == names.count("vkEndCommandBuffer") == 1
            batches = field(call, "pSubmits")
            buffers = field(batches.GetChild(0), "pCommandBuffers")
            assert batches.NumChildren() == buffers.NumChildren() == 1 and buffers.GetChild(0).data.basic.u == command
            details[stage] = dict(dispatch_event=action.eventId, command_buffer=command, begin_chunk=begin,
                                  dispatch_chunk=dispatch, end_chunk=end, submit_chunk=submit,
                                  queue=field(call, "queue").data.basic.u, cpu_submit_us=call.metadata.durationMicro)
        assert set(details) == set(stages) and not frame.GetFrameInfo().debugMessages
        digest = hashlib.sha256()
        with path.open("rb") as source:
            for block in iter(lambda: source.read(8 * 1024 * 1024), b""):
                digest.update(block)
        return dict(frame=info["frame"], size=info["size"], tool=rd.GetVersionString(),
                    capture=path.relative_to(ROOT).as_posix(), capture_sha256=digest.hexdigest(), stages=details,
                    all_frame_submits=len(submits), all_frame_submit_us=sum(q.metadata.durationMicro for _, q in submits),
                    method="CPU vkQueueSubmit duration inside one captured frame. Excludes command recording, "
                           "the subsequent vkWaitForFences call, and GPU execution.")
    finally:
        if frame:
            frame.Shutdown()
        capture.Shutdown()


def submit_times(exe, renderdoc):
    from capture_material_studio import run_renderdoc

    stamp = hashlib.sha256(exe.read_bytes()).hexdigest()[:12]
    results = {}
    for mode in ("off", "on"):
        work = ROOT / f"tmp/material-reference/rough-native-{mode}-{stamp}-0"
        env = dict(os.environ, RF_REFLECTION_SUBMIT_WORK=str(work))
        output = work / "cpu-submits.json"
        output.unlink(missing_ok=True)
        run_renderdoc(renderdoc, Path(__file__).resolve(), ROOT, env)
        result = json.loads(output.read_bytes())
        if "error" in result:
            raise RuntimeError(result["error"])
        result.update(scene_reflections=True, denoise_reflections=mode == "on")
        results[mode] = result
    (OUT / "editor-submits.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf8")


if os.environ.get("RF_REFLECTION_SUBMIT_WORK"):
    import sys
    import traceback
    work = Path(os.environ["RF_REFLECTION_SUBMIT_WORK"])
    try:
        data = captured_submits(work)
    except Exception:
        data = {"error": traceback.format_exc()}
    (work / "cpu-submits.json").write_text(json.dumps(data, indent=2), encoding="utf8")
    sys.exit(0)

elif __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=ROOT / "tmp/rough-reflections/Forge-9e48ab1f.exe")
    parser.add_argument("--submits", type=Path, metavar="RENDERDOC", help="从现有原生尺寸截帧读取 CPU 提交时间")
    args = parser.parse_args()
    exe = args.exe.resolve(strict=True)
    if args.submits:
        submit_times(exe, args.submits.resolve(strict=True))
    else:
        editor_times(exe)
