"""Compare two Forge builds on the same saved Native Sponza Lit scene on Windows."""
import argparse
import ctypes
import gzip
import json
import os
from pathlib import Path
import re
from statistics import median
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "output/profiling/compute-memory"
WORK = ROOT / "tmp/compute-memory-viewport"


def profile(exe, label, run, env):
    name = f"{run}-{label}"
    trace_file = WORK / "RedflagProfile-Runtime.json"
    trace_file.unlink(missing_ok=True)
    startup = subprocess.STARTUPINFO()
    startup.dwFlags = subprocess.STARTF_USESHOWWINDOW
    logfile = WORK / (name + ".log")
    with logfile.open("wb") as stream, subprocess.Popen([exe, "--vulkan", "--frames", "27"], cwd=WORK, env=env,
            stdout=stream, stderr=subprocess.STDOUT, startupinfo=startup) as process:
        # Windows can minimize the test window when the foreground editor changes. A minimized
        # window cannot acquire a swapchain image and never reaches the requested frame count.
        def restore(hwnd, _):
            pid = ctypes.c_ulong()
            ctypes.windll.user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            if pid.value == process.pid and ctypes.windll.user32.IsIconic(hwnd):
                ctypes.windll.user32.ShowWindowAsync(hwnd, 4)  # SW_SHOWNOACTIVATE
            return 1

        callback = ctypes.WINFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p)(restore)
        start = time.monotonic()
        while process.poll() is None:
            ctypes.windll.user32.EnumWindows(callback, 0)
            if time.monotonic() - start > 150:
                process.kill()
                raise TimeoutError(f"Native editor did not finish {name}")
            time.sleep(.5)
        if process.returncode:
            raise RuntimeError(f"{name} exited with {process.returncode}. See {logfile}")
    log = logfile.read_text(encoding="utf8", errors="replace")

    pairs = re.findall(r"Lighting preview (\d+) x (\d+), viewport (\d+) x (\d+)", log)
    if not pairs or pairs[-1][0:2] != pairs[-1][2:4]:
        raise ValueError(f"{name} did not run at the panel's actual size")
    extent = list(map(int, pairs[-1][:2]))
    trace = trace_file.read_bytes()
    (OUT / (name + ".json.gz")).write_bytes(gzip.compress(trace, mtime=0))
    events = json.loads(trace)["traceEvents"]
    samples = [e["dur"] / 1000 for e in events if e["name"] == "Lighting accumulation"]
    if len(samples) < 20:
        raise ValueError(f"{name} has only {len(samples)} uncached lighting frames")
    values = samples[4:]
    print(f"{name}: {extent}, {len(values)} measured, median {median(values):.2f} ms", flush=True)
    return dict(name=name, extent=extent, warmup_frames=4, lighting_ms=values, median_ms=median(values))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("host_build", type=Path)
    parser.add_argument("local_build", type=Path)
    parser.add_argument("--production", type=Path, help="also verify the final Forge executable with the same workload")
    args = parser.parse_args()
    a, b = args.host_build.resolve(), args.local_build.resolve()
    WORK.mkdir(parents=True, exist_ok=True)
    OUT.mkdir(parents=True, exist_ok=True)
    for child in ("assets", "Resources", "mono"):
        target = ROOT / "RedflagForge" / child
        link = WORK / child
        if not link.exists():
            subprocess.run(["cmd", "/c", "mklink", "/J", link, target], check=True, capture_output=True)
    env = os.environ.copy()
    env["PATH"] = str(ROOT / "bin/Debug-windows-x86_64/RedflagForge") + os.pathsep + env["PATH"]
    env["RF_VULKAN_VALIDATION"] = "1"
    order = [(a, "host"), (b, "local"), (b, "local"), (a, "host")]
    rows = [profile(exe, label, i, env) for i, (exe, label) in enumerate(order, 1)]
    if any(r["extent"] != rows[0]["extent"] for r in rows):
        raise ValueError("Window layout changed between runs")
    report = dict(scene="Sponza-Atrium.redflag", shading="Native Lit", build="Debug", validation=True, unit="ms",
                  measurement="CPU wall time for synchronous compute submissions and GPU completion",
                  runs=rows)
    for label in ("host", "local"):
        values = [v for r in rows if r["name"].endswith(label) for v in r["lighting_ms"]]
        report[label + "_median_ms"] = median(values)
    if args.production:
        report["production"] = profile(args.production.resolve(), "production", 5, env)
    (OUT / "native.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf8")


if __name__ == "__main__":
    main()
