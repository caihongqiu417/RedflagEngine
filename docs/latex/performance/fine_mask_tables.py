"""从 Sponza 同机位实测记录生成镂空边缘的 GPU 和编辑器耗时表。"""
import json
from pathlib import Path
from statistics import median, quantiles


directory = Path(__file__).resolve().parent
evidence = directory.parents[2] / "output/profiling/fine-mask"
gpu = json.loads((evidence / "gpu.json").read_bytes())["cases"]
editor = json.loads((evidence / "editor.json").read_bytes())
cpu = editor["summary"]
old, new = gpu["old"], gpu["new"]
assert all(gpu[key]["scene_sha256"] == editor["scene_sha256"] and
           gpu[key]["settings_sha256"] == editor["settings_sha256"] and
           editor["executable_sha256"][key].startswith(gpu[key]["executable_sha_prefix"]) for key in ("old", "new"))
assert all(g["draws"] == 228 and g["dispatches"] == 12 for g in gpu.values())
rows = []
names = {"copy_previous_surface": "复制上一帧表面", "cluster": "分簇点光源着色",
         "probe_update": "更新 DDGI 探针", "probe_shade": "按探针照亮表面",
         "reflection": "场景反射", "fog": "体积雾"}
for key, label in names.items():
    a, b = old["stages"][key], new["stages"][key]
    assert len(a["gpu_ms"]) == len(b["gpu_ms"]) == 12 and a["event"] == b["event"]
    cells = [label, str(a["event"])]
    for stage in (a, b):
        q = quantiles(stage["gpu_ms"], n=10, method="inclusive")
        cells.extend([f"{median(stage['gpu_ms']):.3f}", f"{q[0]:.3f}--{q[8]:.3f}"])
    rows.append(" & ".join(cells))

cpu_rows = []
for key, label in (("frame_ms", "重画场景，整帧中位数（ms）"), ("cached_ms", "沿用上一张图，整帧中位数（ms）")):
    a, b = cpu["old"][key], cpu["new"][key]
    assert a["samples"] == b["samples"] == (72 if key == "frame_ms" else 48)
    cpu_rows.append(f"{label} & {a['p50_ms']:.2f} & {b['p50_ms']:.2f}")
cpu_rows.append("整个编辑器进程的 CPU 用时，中位数（s） & %.2f & %.2f" %
                (cpu["old"]["process_cpu_median_ms"]/1000, cpu["new"]["process_cpu_median_ms"]/1000))
assert old["queue_submit_count"] == new["queue_submit_count"] == 14
cpu_rows.append("截帧中 14 次队列提交的 CPU 用时之和（ms） & %.3f & %.3f" %
                (sum(old["queue_submit_cpu_us"])/1000, sum(new["queue_submit_cpu_us"])/1000))

commands = []
for name, values in (("fineGpuRows", rows), ("fineCpuRows", cpu_rows)):
    commands.append(r"\newcommand{\%s}{%%" % name)
    commands.extend(row + r"\\" for row in values)
    commands.append("}")
(directory / "build/fine-mask-rows.tex").write_text("\n".join(commands) + "\n", encoding="utf8")
