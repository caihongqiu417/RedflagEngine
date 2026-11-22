"""Measure a saved Sponza editor view during zoom, normal navigation and stationary reuse."""
import argparse
import ctypes
import gzip
import hashlib
import json
from pathlib import Path
import re
import shutil
from statistics import median, quantiles
import subprocess
import time

import win32api
import win32con
import win32gui
import win32process
import yaml

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / "tmp/rhi-vulkan/navigation-light"
OUT = ROOT / "output/profiling/navigation-light"
SIZE = (948, 613)
MARKERS = {"Lighting camera motion", "Lighting navigation", "Lighting accumulation", "Lighting cached"}


def prepare(work=WORK):
    work.mkdir(parents=True, exist_ok=True)
    for child in ("mono", "Resources", "assets"):
        source = ROOT / "RedflagForge" / child
        if child == "assets":
            paths = list(source.iterdir())
        else:
            paths = [source]
        for path in paths:
            target = work / path.relative_to(ROOT / "RedflagForge")
            if path.name == "scenes":
                target.mkdir(parents=True, exist_ok=True)
                shutil.copy2(path / "Sponza-Atrium.redflag", target)
            elif path.is_dir() and not target.exists():
                target.parent.mkdir(parents=True, exist_ok=True)
                subprocess.run(["cmd", "/c", "mklink", "/J", target, path], check=True, capture_output=True)


def until(process, predicate, seconds=60):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        if process.poll() is not None:
            raise RuntimeError(f"Editor exited with {process.returncode}")
        value = predicate()
        if value: return value
        time.sleep(.15)
    raise TimeoutError(f"Editor did not reach {predicate.__name__}")


def window_for(pid):
    found = []
    def collect(hwnd, _):
        if win32process.GetWindowThreadProcessId(hwnd)[1] == pid and win32gui.GetWindowText(hwnd) == "Redflag Forge":
            found.append(hwnd)
    win32gui.EnumWindows(collect, None)
    return found[0] if found else None


def set_exact_view(process, hwnd, log):
    until(process, lambda: b"Lighting preview " in log.read_bytes(), 90)
    w, h = 1783, 1153
    previous = None
    for attempt in range(8):
        size_of_log = log.stat().st_size
        win32gui.SetWindowPos(hwnd, 0, 28, 28, w, h, win32con.SWP_NOACTIVATE | win32con.SWP_NOZORDER)
        def logged_size():
            if win32gui.IsIconic(hwnd):
                win32gui.ShowWindow(hwnd, win32con.SW_SHOWNOACTIVATE)
            with log.open("rb") as f:
                f.seek(size_of_log)
                pairs = re.findall(rb"Lighting preview (\d+) x (\d+), viewport (\d+) x (\d+)", f.read())
            if not pairs or pairs[-1][:2] != pairs[-1][2:]:
                return None
            return tuple(map(int, pairs[-1][:2]))
        size = until(process, logged_size, 75 if attempt == 0 else 25)
        print("Client tuning", w, h, "viewport", size, flush=True)
        if size == SIZE: return
        slope_x, slope_y = .55, .68
        if previous:
            px, py, pw, ph = previous
            if w != pw and size[0] != px: slope_x = (size[0] - px) / (w - pw)
            if h != ph and size[1] != py: slope_y = (size[1] - py) / (h - ph)
        previous = (*size, w, h)
        w += round((SIZE[0]-size[0])/slope_x)
        h += round((SIZE[1]-size[1])/slope_y)
    raise RuntimeError("Could not reproduce the 948 x 613 viewport")


def save_view(hwnd, path):
    import win32ui
    from PIL import Image

    _, _, width, height = win32gui.GetClientRect(hwnd)
    handle = win32gui.GetDC(hwnd)
    source = win32ui.CreateDCFromHandle(handle)
    dest = source.CreateCompatibleDC()
    bitmap = win32ui.CreateBitmap()
    bitmap.CreateCompatibleBitmap(source, width, height)
    dest.SelectObject(bitmap)
    try:
        if not ctypes.windll.user32.PrintWindow(hwnd, dest.GetSafeHdc(), 3):
            raise RuntimeError("Cannot capture the Forge client")
        pixels = bitmap.GetBitmapBits(True)
        Image.frombytes("RGB", (width,height), pixels, "raw", "BGRX", 0, 1).save(path)
    finally:
        dest.DeleteDC()
        win32gui.DeleteObject(bitmap.GetHandle())
        win32gui.ReleaseDC(hwnd, handle)


def measure(exe, fast, env):
    mode = "navigation" if fast else "lit-motion"
    config = "Sponza-Atrium.redflag.render.yaml"
    settings = yaml.safe_load((ROOT/"RedflagForge/assets/scenes"/config).read_text(encoding="utf8"))
    settings.update(FastNavigation=fast, PreviewWidth=0, Shading="Lit")
    (WORK/"assets/scenes"/config).write_text(yaml.safe_dump(settings), encoding="utf8")
    (WORK/"imgui-vulkan.ini").unlink(missing_ok=True)
    raw = WORK/"RedflagProfile-Runtime.json"
    raw.unlink(missing_ok=True)
    log = WORK/(mode + ".log")
    with log.open("wb") as output, subprocess.Popen([exe, "--vulkan"], cwd=WORK, env=env,
          stdout=output, stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW) as app:
        hwnd = None
        original_cursor = win32api.GetCursorPos()
        try:
            hwnd = until(app, lambda: window_for(app.pid))
            win32gui.ShowWindow(hwnd, win32con.SW_RESTORE)
            set_exact_view(app, hwnd, log)
            def has_cached():
                if not raw.exists(): return False
                with raw.open("rb") as f:
                    f.seek(max(0, raw.stat().st_size - 65536))
                    return f.read().count(b"Lighting cached") > 8
            until(app, has_cached)
            rect = win32gui.GetClientRect(hwnd)
            point = int(rect[2]*.49), int(rect[3]*.37)
            desktop = win32gui.ClientToScreen(hwnd, point)
            try: win32gui.SetForegroundWindow(hwnd)
            except Exception: pass
            win32api.SetCursorPos(desktop)
            win32gui.PostMessage(hwnd, win32con.WM_MOUSEMOVE, 0, win32api.MAKELONG(*point))
            time.sleep(.35)
            start_us = time.perf_counter_ns() // 1000
            for i in range(50):
                delta = 120 if (i//5) % 2 == 0 else -120
                win32gui.PostMessage(hwnd, win32con.WM_MOUSEWHEEL, (delta & 65535) << 16,
                                     win32api.MAKELONG(*desktop))
                time.sleep(.15)
            time.sleep(2)
            end_us = time.perf_counter_ns() // 1000

            # 同一路线完成计时后再截图，截图开销不计入统计。
            win32gui.PostMessage(hwnd, win32con.WM_MOUSEWHEEL, 120 << 16, win32api.MAKELONG(*desktop))
            time.sleep(.09)
            save_view(hwnd, OUT/(mode+"-move.png"))
            time.sleep(2)
            save_view(hwnd, OUT/(mode+"-rest.png"))
        finally:
            if hwnd:
                if "desktop" in locals() and win32api.GetCursorPos() == desktop:
                    win32api.SetCursorPos(original_cursor)
                win32gui.PostMessage(hwnd, win32con.WM_CLOSE, 0, 0)
            try: app.wait(20)
            except subprocess.TimeoutExpired:
                app.kill()
                app.wait()
    if app.returncode: raise RuntimeError(f"Editor exited with {app.returncode}")
    trace = raw.read_bytes()
    (OUT/("final-"+mode+".json.gz")).write_bytes(gzip.compress(trace, mtime=0))
    events = json.loads(trace)["traceEvents"]
    groups = {}
    frames = sorted((e for e in events if e["name"] == "RunLoop" and start_us <= e["ts"] <= end_us), key=lambda e:e["ts"])
    markers = iter(sorted((e for e in events if e["name"] in MARKERS and e["ts"] >= start_us), key=lambda e:e["ts"]))
    marker = next(markers, None)
    for frame in frames:
        while marker and marker["ts"] < frame["ts"]:
            marker = next(markers, None)
        if marker and marker["ts"] < frame["ts"] + frame["dur"]:
            groups.setdefault(marker["name"],[]).append(frame["dur"] / 1000)
            marker = next(markers, None)
    for required in (("Lighting navigation" if fast else "Lighting camera motion"),"Lighting cached"):
        if len(groups.get(required,[])) < 5:
            raise ValueError(f"{mode}: insufficient frames for {required}")
    return {key: dict(frames=len(values), p50_ms=median(values), p95_ms=quantiles(values,n=20,method="inclusive")[18])
            for key, values in groups.items()}


if __name__ == "__main__":
    import os
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exe",type=Path)
    args=parser.parse_args()
    exe=args.exe.resolve()
    ctypes.windll.user32.SetProcessDPIAware()
    prepare()
    OUT.mkdir(parents=True,exist_ok=True)
    env=os.environ.copy()
    env["PATH"]=str(ROOT/"bin/Debug-windows-x86_64/RedflagForge")+os.pathsep+env["PATH"]
    env["RF_VULKAN_VALIDATION"]="1"
    report=dict(scene="Sponza-Atrium.redflag", viewport=SIZE, build="Debug", validation=True,
                executable_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
                source="Whole RunLoop wall time including GPU waits and presentation. 50 wheel events, 150ms apart.",
                lit_motion=measure(exe,False,env),navigation=measure(exe,True,env))
    (OUT/"final-navigation.json").write_text(json.dumps(report,indent=2)+"\n",encoding="utf8")
    print(json.dumps(report,indent=2))
