"""核对报告对应的原始帧，将两项离屏测试的逐帧纳秒值换算为微秒。"""
import csv
import hashlib
import json
from pathlib import Path
from statistics import median, quantiles


def interval(samples):
    tenths = quantiles(samples, n=10, method="inclusive")
    return f"{median(samples):.1f} & {tenths[0]:.1f}--{tenths[8]:.1f}"


def experiment(folder, modes):
    with (folder / "timing.csv").open(encoding="utf-8-sig", newline="") as stream:
        records = list(csv.DictReader(stream))
    for mode, title in modes:
        rows = [row for row in records if row["mode"] == mode]
        if [int(row["round"]) for row in rows] != list(range(1, 121)):
            raise ValueError(f"{folder.name}/{mode}: expected 120 ordered samples")
        cpu = [int(row["cpu_ns"]) / 1000 for row in rows]
        gpu = [int(row["gpu_ns"]) / 1000 for row in rows]
        yield f"{title} & {interval(cpu)} & {interval(gpu)} " + r"\\"


def check_capture(folder, report):
    capture = folder / report["capture"]
    if hashlib.sha256(capture.read_bytes()).hexdigest() != report["sha256"]:
        raise ValueError(f"{capture.name}: capture changed; replay it before rebuilding the report")


if __name__ == "__main__":
    source = Path(__file__).resolve().parent
    data = source.parents[2] / "output/profiling"
    sprite_report = json.loads((data / "batch2d/replay.json").read_text(encoding="utf-8"))
    check_capture(data / "batch2d", sprite_report)
    mesh_report = json.loads((data / "mesh/replay.json").read_text(encoding="utf-8"))
    for capture in mesh_report["captures"].values():
        check_capture(data / "mesh", capture)
    lines = [
        *experiment(data / "batch2d", [("individual", "精灵逐个提交"), ("batch", "精灵合成一批")]),
        r"\midrule",
        *experiment(data / "mesh", [("individual", "独立网格逐个画"), ("instanced", "共享网格实例化"),
                                    ("prez_instanced", "实例化加深度预写")]),
    ]
    table = "\\newcommand{\\drawTimingRows}{%\n" + "\n".join(lines) + "\n}\n"
    (source / "build/timing-rows.tex").write_text(table, encoding="utf-8")
