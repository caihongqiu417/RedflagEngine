"""Typeset the measured compute event IDs and GPU times from the retained captures."""
import hashlib
import gzip
import json
from pathlib import Path
from statistics import median

sources = Path(__file__).resolve().parents[3] / "output/profiling"
groups = [
    ("gi", "SponzaGI", "giTimingRows", ["更新探针", "在表面查询探针", "直接光候选", "直接光复用", "间接光候选", "间接光重连"]),
    ("screen-effects", "SponzaScreen", "screenTimingRows", ["环境遮蔽", "屏幕反射", "时间抗锯齿", "筛选候选灯", "用候选灯着色"]),
]
lines = []
for folder, name, macro, labels in groups:
    capture = sources / folder
    measurements = json.loads((capture / "renderdoc.json").read_text())
    assert measurements["result"] == "ok"
    assert hashlib.sha256((capture / (name + "_capture.rdc")).read_bytes()).hexdigest() == measurements["sha256"]
    assert len(labels) == len(measurements["dispatches"])
    lines.append("\\newcommand{\\" + macro + "}{%")
    for label, event in zip(labels, measurements["dispatches"]):
        samples = event["replay_gpu_samples_ms"]
        assert len(samples) == 9
        shape = r"$\times$".join(str(n) for n in event["groups"] if n > 1)
        lines.append(f"{label} & {event['eid']} & {shape} & {event['replay_gpu_median_ms']:.3f} & "
                     + f"{min(samples):.3f}--{max(samples):.3f}" + r"\\")
    lines.append("}")
memory = sources / "compute-memory"
native = json.loads((memory / "native.json").read_text())
run_medians = []
all_samples = {"host": [], "local": []}
for run in native["runs"]:
    trace = json.loads(gzip.decompress((memory / (run["name"] + ".json.gz")).read_bytes()))
    times = [e["dur"] / 1000 for e in trace["traceEvents"] if e["name"] == "Lighting accumulation"]
    samples = times[run["warmup_frames"]:]
    assert samples == run["lighting_ms"] and len(samples) == 21
    run_medians.append(f"{median(samples):.2f}")
    all_samples[run["name"].split("-")[1]].extend(samples)
old, new = [median(all_samples[key]) for key in ("host", "local")]
for macro, value in (("memoryRunTimes", " & ".join(run_medians)), ("memoryOld", f"{old:.1f}"),
                     ("memoryNew", f"{new:.1f}"), ("memoryRatio", f"{old/new:.2f}")):
    lines.append(r"\newcommand{" + "\\" + macro + "}{" + value + "}")

memory_frames = json.loads((memory / "replay.json").read_text())["captures"]
for frame in memory_frames.values():
    assert hashlib.sha256((sources.parents[1] / frame["path"]).read_bytes()).hexdigest() == frame["sha256"]
labels = {"probe-update": "更新光照探针", "restir-gi": "重用间接光样本", "gtao": "邻近表面的环境遮蔽"}
lines.append(r"\newcommand{\memoryGpuRows}{%")
for host, local in zip(memory_frames["host"]["events"], memory_frames["local"]["events"]):
    assert host["name"] == local["name"] and host["groups"] == local["groups"]
    assert host.get("rgba32f_sha256") == local.get("rgba32f_sha256")
    if host["name"] in labels:
        assert len(host["gpu_ms"]) == len(local["gpu_ms"]) == 7
        lines.append(f"{labels[host['name']]} & {host['eid']} & {median(host['gpu_ms']):.3f} & "
                     f"{median(local['gpu_ms']):.3f}" + r"\\")
lines.append("}")
navigation = json.loads((memory / "final-navigation.json").read_text())
original = json.loads((sources.parent / "rhi/editor-quality.json").read_text())
assert navigation["viewport"] == original["viewport"] == [948, 613]
lines.append(r"\newcommand{\memoryNavigationRows}{%")
for label, mode, earlier, marker in [
    ("移动时保持组合光照", "lit_motion", "rawmotion", "Lighting camera motion"),
    ("移动时使用底色", "navigation", "native", "Lighting navigation"),
    ("停手后沿用静止图", "navigation", "native", "Lighting cached"),
]:
    before = original["frame_times_ms"][earlier][marker]
    after = navigation[mode][marker]
    lines.append(f"{label} & {before['p50']:.2f} & {after['p50_ms']:.2f} & "
                 f"{after['p95_ms']:.2f} & {after['frames']}" + r"\\")
lines.append("}")
reflections = json.loads((sources / "scene-reflections/frames.json").read_bytes())["cases"]
lines.append(r"\newcommand{\mirrorTimingRows}{%")
for scene, title in [("mirror", "镜子工作室"), ("bathroom", "RTXGI 浴室")]:
    for mode, label in [("screen", "只查屏幕"), ("scene", "再查场景")]:
        frame = reflections[scene + "-" + mode]
        event = frame["reflection"]
        samples = event["gpu_ms"]
        assert len(samples) == 11 and event["event"] in frame["dispatches"]
        assert event["scene_trace"] == (mode == "scene")
        width, height = frame["size"]
        size = str(width) + r"\(\times\)" + str(height)
        pixels = f'{frame["reflection_hit_pixels"]:,}'.replace(",", r"\,")
        lines.append(f"{title} {size} & {label} & {event['event']} & {pixels} & {median(samples):.3f}" + r"\\")
lines.append("}")
rough = json.loads((sources / "rough-reflections/bathroom.json").read_bytes())
quality = rough["metrics"]
assert quality["samples"] == 256 and quality["size"] == rough["cases"]["small-off"]["size"]
for key, macro in (("mse_raw", "roughRawMSE"), ("mse_filtered", "roughFilteredMSE"),
                   ("half_reference_mse", "roughHalfMSE")):
    lines.append(r"\newcommand{" + "\\" + macro + "}{" + f"{quality[key]:.6f}" + "}")
lines.append(r"\newcommand{\roughTimingRows}{%")
for mode in ("small", "native"):
    for enabled in (False, True):
        case = rough["cases"][mode + ("-on" if enabled else "-off")]
        w, h = case["size"]
        trace = case["reflection"]["gpu_ms"]
        blur = case["reflection"].get("filter", {}).get("gpu_ms", [])
        assert len(trace) == 11 and len(blur) == (11 if enabled else 0)
        assert len(case["draws"]) == 195 and len(case["dispatches"]) == 10 + int(enabled)
        assert case["reflection"]["event"] == 556 and (not enabled or case["reflection"]["filter"]["event"] == 568)
        assert case["scratch_bytes"] == (w*h*16 if enabled else 0)
        total = median([a+b for a, b in zip(trace, blur)] if blur else trace)
        assert abs(total - case["median_reflections_ms"]) < .00001
        blur_time = f"{median(blur):.3f}" if blur else "---"
        lines.append(f"{w}\\(\\times\\){h} & {'开' if enabled else '关'} & {median(trace):.3f} & {blur_time} & "
                     f"{total:.3f} & {case['scratch_bytes']/1048576:.2f}" + r"\\")
lines.append("}")
editor = json.loads((sources / "rough-reflections/editor-timing.json").read_bytes())
assert editor["executable_sha256"] == rough["executable_sha256"] and editor["size"] == rough["cases"]["native-on"]["size"]
lines.append(r"\newcommand{\roughEditorRows}{%")
for mode, label in (("default", "关闭 SSR"), ("raw", "开启 SSR"), ("filtered", "开启 SSR 和降噪")):
    summary = editor["summary"][mode]
    redrawn, cached = summary["redrawn_frame_ms"], summary["cached_frame_ms"]
    assert redrawn["samples"] == 72 and cached["samples"] == 48
    lines.append(f"{label} & {redrawn['p50_ms']:.2f} & {redrawn['p95_ms']:.2f} & {cached['p50_ms']:.2f}" + r"\\")
lines.append("}")
submit = json.loads((sources / "rough-reflections/editor-submits.json").read_bytes())
for mode in ("off", "on"):
    assert submit[mode]["capture_sha256"] == rough["cases"]["native-" + mode]["capture_sha256"]
for macro, mode, stage in (("roughSubmitOff", "off", "reflection"), ("roughSubmitOn", "on", "reflection"),
                           ("roughSubmitFilter", "on", "filter")):
    event = submit[mode]["stages"][stage]
    assert event["dispatch_event"] == (568 if stage == "filter" else 556)
    lines.append(r"\newcommand{" + "\\" + macro + "}{" + str(event["cpu_submit_us"]) + "}")
mask_frame = json.loads((sources / "gi-mask/sponza-timing.json").read_bytes())
old, new = (mask_frame["cases"][key] for key in ("before", "after"))
assert old["scene_sha256"] == new["scene_sha256"] and old["settings_sha256"] == new["settings_sha256"]
assert old["size"] == new["size"] == [384, 246] and old["frame"] == new["frame"] == 24
assert old["draws"] == new["draws"] == 228 and old["dispatches"] == new["dispatches"] == 12
lines.append(r"\newcommand{\maskTimingRows}{%")
for name, label in (("cluster", "分簇点光源着色"), ("probe", "更新 DDGI 探针"),
                    ("reflection", "屏幕内外的反射追踪"), ("fog", "体积雾（8 步）")):
    a, b = old["stages"][name], new["stages"][name]
    assert a["groups"] == b["groups"] and a["event"] == b["event"]
    assert len(a["gpu_ms"]) == len(b["gpu_ms"]) == 11
    earlier, later = median(a["gpu_ms"]), median(b["gpu_ms"])
    lines.append(f"{label} & {a['event']} & {earlier:.3f} & {later:.3f} & \\({later-earlier:+.3f}\\)" + r"\\")
lines.append("}")
Path("build/compute-timing.tex").write_text("\n".join(lines) + "\n", encoding="utf-8")
