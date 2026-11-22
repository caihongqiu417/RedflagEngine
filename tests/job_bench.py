"""从维修站提取真实变换，汇总相邻两次执行的耗时。Release 基准保留每一对原始样本。"""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import platform
from statistics import median
import yaml

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT/"output/profiling/jobs"
SCENE = ROOT/"RedflagForge/assets/scenes/Watt-Station.redflag"
HEIGHT = ROOT/"RedflagForge/assets/terrain/quarry/quarry-height.png"


def transforms():
    scene = yaml.safe_load(SCENE.read_bytes())
    path = ROOT/"bin-int/Tests/job-bench/transforms.txt"
    with path.open("w",encoding="ascii") as file:
        for entity in scene["Entities"]:
            transform = entity.get("TransformComponent")
            if transform:
                values = [v for k in ("Translation","Rotation","Scale") for v in transform[k]]
                print(*values,file=file)


def summary():
    report = {"build":"MSVC x64 /O2 C++17","units":"microseconds", "cpu":platform.processor(),
              "scene_sha256":hashlib.sha256(SCENE.read_bytes()).hexdigest(),
              "heightmap_sha256":hashlib.sha256(HEIGHT.read_bytes()).hexdigest(),"groups":[]}
    for name,keys,metrics in (("terrain",["chunk_cells","workers"],["serial_us","parallel_us","decode_us"]),
                              ("ecs",["entities"],["serial_us","parallel_us","load_sync_us","load_job_us"]),
                              ("first-load",[],["serial_us","parallel_us"])):
        groups = {}
        with (OUT/(name+".csv")).open() as file:
            for row in csv.DictReader(file):
                groups.setdefault(tuple(row[k] for k in keys),[]).append(row)
        for group,samples in groups.items():
            series = {}
            for metric in metrics:
                times = sorted(float(s[metric]) for s in samples)
                series[metric] = dict(p50=median(times),p95=times[math.ceil(.95*len(times))-1])
            wins = sum(float(r["parallel_us"])<float(r["serial_us"]) for r in samples)
            report["groups"].append(dict(case=name,**dict(zip(keys,map(int,group))),pairs=len(samples),
                                         parallel_faster_pairs=wins,timing=series))
    (OUT/"summary.json").write_text(json.dumps(report,indent=2,ensure_ascii=False)+"\n","utf8")
    print(json.dumps(report,ensure_ascii=False))


if __name__=="__main__":
    cli=argparse.ArgumentParser()
    cli.add_argument("action",choices=["prepare","summary"])
    transforms() if cli.parse_args().action=="prepare" else summary()
