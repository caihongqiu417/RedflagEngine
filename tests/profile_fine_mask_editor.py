"""在项目根目录运行，传入旧版和新版的冻结编辑器。使用 Sponza 性能截帧的相机与面板设置。"""
import argparse
import gzip
import json
import msvcrt
import os
from pathlib import Path
import re
import shutil
from statistics import median
import subprocess

import win32con
import win32event
import win32process
import win32service

from profile_fine_mask import ROOT, OUT, WORK, digest
from profile_native_navigation import prepare
from profile_reflection_editor import frame_samples, metrics


def measure(exe, name, folder, env, log):
    startup = win32process.STARTUPINFO()
    startup.lpDesktop = "WinSta0\\" + name
    startup.dwFlags = win32con.STARTF_USESTDHANDLES
    cmd = subprocess.list2cmdline([str(exe), "--vulkan", "--frames", "56"])
    with log.open("wb") as output, open(os.devnull, "rb") as null:
        startup.hStdOutput = startup.hStdError = msvcrt.get_osfhandle(output.fileno())
        startup.hStdInput = msvcrt.get_osfhandle(null.fileno())
        for handle in (startup.hStdOutput, startup.hStdInput):
            os.set_handle_inheritable(int(handle), True)
        process, thread, _, _ = win32process.CreateProcess(None, cmd, None, None, True,
            win32con.CREATE_NO_WINDOW, env, str(folder), startup)
        try:
            if win32event.WaitForSingleObject(process, 180000) != win32con.WAIT_OBJECT_0:
                win32process.TerminateProcess(process, 1)
                raise TimeoutError(cmd)
            assert win32process.GetExitCodeProcess(process) == 0, log
            cpu = win32process.GetProcessTimes(process)
        finally:
            thread.Close()
            process.Close()
    # Windows 以 100 纳秒为单位累计这个进程所有线程实际得到的 CPU 时间。
    return (cpu["UserTime"] + cpu["KernelTime"]) / 10000.


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("old", type=Path)
    parser.add_argument("new", type=Path)
    parser.add_argument("--capture-setup", type=Path, default=ROOT / "tmp/material-reference/Sponza-Mask-532eca6d7921-384")
    args = parser.parse_args()
    programs = {"old": args.old.resolve(strict=True), "new": args.new.resolve(strict=True)}
    folder = WORK / "editor"
    prepare(folder)
    entry = folder / "assets/scenes/Sponza-Atrium.redflag"
    for suffix in ("", ".render.yaml"):
        source = args.capture_setup / "assets/scenes" / (entry.name + suffix)
        shutil.copyfile(source, str(entry) + suffix)
    env = dict(os.environ, PATH=str(ROOT / "bin/Debug-windows-x86_64/RedflagForge") + os.pathsep + os.environ["PATH"])
    for key in ("RF_VULKAN_VALIDATION", "VK_INSTANCE_LAYERS", "VK_ADD_LAYER_PATH", "ENABLE_VULKAN_RENDERDOC_CAPTURE"):
        env.pop(key, None)
    traces = OUT / "traces"
    traces.mkdir(parents=True, exist_ok=True)
    name = f"FineMaskTiming{os.getpid()}"
    desktop = win32service.CreateDesktop(name, 0, win32con.GENERIC_ALL, None)
    try:
        runs = []
        for r, version in enumerate(("old", "new", "new", "old", "old", "new")):
            tag = f"{r + 1}-{version}"
            (folder / "imgui-vulkan.ini").unlink(missing_ok=True)
            log = folder / (tag + ".log")
            cpu = measure(programs[version], name, folder, env, log)
            text = log.read_bytes()
            sizes = re.findall(rb"Lighting preview (\d+) x (\d+), viewport (\d+) x (\d+)", text)
            w, h, vw, vh = map(int, sizes[-1])
            assert (w, h) == (384, 246)
            trace = (folder / "RedflagProfile-Runtime.json").read_bytes()
            (traces / (tag + ".json.gz")).write_bytes(gzip.compress(trace, mtime=0))
            drawn, cached = frame_samples(json.loads(trace)["traceEvents"])
            row = dict(version=version, full_process_cpu_ms=cpu, frame_ms=[s[1] for s in drawn],
                       cached_ms=[s[1] for s in cached], lighting_ms=[s[2] for s in drawn],
                       viewport=[vw, vh], render_size=[w, h], trace="traces/" + tag + ".json.gz")
            runs.append(row)
            print(tag, w, h, "CPU ms", cpu, "frame ms", median(row["frame_ms"]), flush=True)
    finally:
        desktop.CloseDesktop()
    summary = {}
    for version in programs:
        rows = [r for r in runs if r["version"] == version]
        summary[version] = {key: metrics([t for row in rows for t in row[key]]) for key in ("frame_ms", "cached_ms", "lighting_ms")}
        summary[version]["process_cpu_median_ms"] = median(r["full_process_cpu_ms"] for r in rows)
    report = dict(size=[384, 246], executable_sha256={key: digest(exe) for key, exe in programs.items()},
                  device=re.search(rb"Vulkan device: ([^\r\n]+)", text)[1].decode(),
                  renderdoc=False, validation_layer=False, scene_sha256=digest(entry),
                  settings_sha256=digest(Path(str(entry) + ".render.yaml")), runs=runs, summary=summary,
                  scope="GetProcessTimes is the sum of user and kernel CPU time on all threads from launch to exit "
                        "(includes model import, all 56 editor frames and shutdown). RunLoop is main-thread wall time "
                        "including GPU waits and presentation. After final resize: 24 redrawn and 16 cached frames per launch.")
    (OUT / "editor.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf8")


if __name__ == "__main__":
    main()
