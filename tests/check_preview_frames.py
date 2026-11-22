"""在独立窗口桌面运行 Vulkan 编辑器，数镜头不动时实际重画和沿用旧图的帧。"""
from collections import Counter
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

import win32con
import win32event
import win32process
import win32service
import yaml

import profile_native_navigation as navigation


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    baked = parser.add_mutually_exclusive_group()
    baked.add_argument("--baked-bathroom", action="store_true", help="Check the supplied RTXGI Bathroom probes")
    baked.add_argument("--baked-scene", type=Path, help="Check the .rfprobes saved beside a scene")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    scenes = root / "RedflagForge/assets/scenes"
    baked_scene = scenes / "RTXGI-Bathroom.redflag" if args.baked_bathroom else args.baked_scene
    source = baked_scene.resolve(strict=True) if baked_scene else scenes / "Sponza-Atrium.redflag"
    work = root / "tmp/rhi-vulkan" / (f"baked-{source.stem}" if baked_scene else "preview-frame-check")
    navigation.prepare(work)
    scene = "Sponza-Atrium.redflag.render.yaml"
    if baked_scene:
        # 编辑器从临时目录打开场景，读的是和场景一起复制过来的烘焙文件。
        shutil.copyfile(source, work / "assets/scenes/Sponza-Atrium.redflag")
        shutil.copyfile(str(source) + ".rfprobes", work / "assets/scenes/Sponza-Atrium.redflag.rfprobes")
    defaults = yaml.safe_load(Path(str(source) + ".render.yaml").read_text(encoding="utf8"))
    binaries = root / "bin/Debug-windows-x86_64/RedflagForge"
    exe = work / "RedflagForge.exe"
    shutil.copyfile(binaries / exe.name, exe)
    env = dict(os.environ, PATH=str(binaries) + os.pathsep + os.environ["PATH"], RF_VULKAN_VALIDATION="1")

    name = f"RedflagPreviewFrames{os.getpid()}"
    desktop = win32service.CreateDesktop(name, 0, win32con.GENERIC_ALL, None)
    start = win32process.STARTUPINFO()
    start.lpDesktop = "WinSta0\\" + name
    results = {}
    cases = [
        ("direct", "All point lights", "Off", False, 1),
        ("clustered", "Clustered lights", "Off", False, 1),
        ("restir-di", "ReSTIR DI", "Off", False, 32),
        ("taa", "All point lights", "Off", True, 32),
        ("ddgi", "All point lights", "DDGI", False, 32),
    ]
    if baked_scene:
        cases = [
            ("baked", "All point lights", "DDGI", False, 1),
            ("baked-taa", "All point lights", "DDGI", True, 32),
            ("baked-ddgi", "All point lights", "DDGI", False, 1),
        ]
    for label, direct, indirect, taa, expected in cases:
        shading = "DDGI" if label == "baked-ddgi" else "Lit"
        settings = dict(defaults, Shading=shading, Direct=direct, Indirect=indirect,
                        TAA=taa, GTAO=False, SSR=False, FastNavigation=False, PreviewWidth=160)
        (work / "assets/scenes" / scene).write_text(yaml.safe_dump(settings), encoding="utf8")
        trace = work / "RedflagProfile-Runtime.json"
        trace.unlink(missing_ok=True)
        command = subprocess.list2cmdline([str(exe), "--vulkan", "--frames", "48"])
        process, thread, _, _ = win32process.CreateProcess(None, command, None, None, False, 0,
                                                           env, str(work), start)
        try:
            if win32event.WaitForSingleObject(process, 90000) != win32con.WAIT_OBJECT_0:
                win32process.TerminateProcess(process, 1)
                raise TimeoutError(label)
            if win32process.GetExitCodeProcess(process) != 0:
                raise RuntimeError(f"Forge failed in {label}")
        finally:
            thread.Close()
            process.Close()
        frames = [e["name"] for e in json.loads(trace.read_bytes())["traceEvents"]
                  if e.get("name") in navigation.MARKERS]
        if not frames or frames[-1] != "Lighting cached":
            raise AssertionError((label, frames))
        # 停靠面板在启动时会调整一次大小。只数最后一次相机投影改变之后的画面。
        last_motion = max(i for i, frame in enumerate(frames) if frame == "Lighting camera motion")
        counts = Counter(frames[last_motion:])
        cached = counts["Lighting cached"]
        drawn = len(frames) - last_motion - cached
        if drawn != expected:
            raise AssertionError((label, dict(counts), expected))
        results[label] = {"startup_before_last_resize": last_motion, "drawn": drawn,
                          "cached": cached, "events": dict(counts)}
        print(label, drawn, cached, flush=True)
    desktop.CloseDesktop()
    report = {"scene": source.name, "preview_width": 160, "application_frames": 48,
              "exe_sha256": hashlib.sha256(exe.read_bytes()).hexdigest(), "cases": results}
    out = root / "output/profiling/navigation-light/preview-frames.json"
    if args.baked_bathroom:
        out = out.with_name("baked-frames.json")
    elif baked_scene:
        out = out.with_name(f"baked-{source.stem}.json")
    out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf8")


if __name__ == "__main__":
    main()
