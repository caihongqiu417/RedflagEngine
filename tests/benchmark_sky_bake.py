"""在同一份场景上交错运行两个正式 Forge 烘焙程序，记录从启动到退出的等待时间。"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
from statistics import median, quantiles
import subprocess
import tempfile
from time import perf_counter

import yaml

from profile_native_navigation import ROOT, prepare


def digest(file):
    with file.open("rb") as data:
        return hashlib.file_digest(data, "sha256").hexdigest()


def measure(first, revised, source, out):
    # 共用同一批资产；只在本次新建的目录删除上一次生成的探针文件。
    work = Path(tempfile.mkdtemp(prefix="sky-bake-timing-", dir=ROOT / "tmp"))
    prepare(work)
    scene = work / "assets/scenes" / source.name
    shutil.copyfile(source, scene)
    settings = Path(str(scene) + ".render.yaml")
    shutil.copyfile(str(source) + ".render.yaml", settings)
    probes = Path(str(scene) + ".rfprobes")
    sky = scene.parent / yaml.safe_load(scene.read_bytes())["Environment"]
    binaries = ROOT / "bin/Debug-windows-x86_64/RedflagForge"
    executables = {"first": first, "revised": revised}
    env = dict(os.environ, PATH=str(binaries) + os.pathsep + os.environ["PATH"], RF_VULKAN_VALIDATION="1")
    for key in ("VK_ADD_LAYER_PATH", "VK_INSTANCE_LAYERS", "ENABLE_VULKAN_RENDERDOC_CAPTURE"):
        env.pop(key, None)
    report = dict(configuration="Windows Debug|x64, Vulkan validation enabled, no RenderDoc capture",
                  scene=scene.name, scene_sha256=digest(scene), render_settings=yaml.safe_load(settings.read_bytes()),
                  settings_sha256=digest(settings), hdr_sha256=digest(sky), executable_sha256={k: digest(v) for k,v in executables.items()},
                  warmup_pairs=2, measured_pairs=10, runs=[])
    for pair in range(-report["warmup_pairs"], report["measured_pairs"]):
        order = ("revised", "first") if pair % 2 else ("first", "revised")
        for label in order:
            probes.unlink(missing_ok=True)
            log = work / f"{pair}-{label}.log"
            with log.open("wb") as stream:
                start = perf_counter()
                subprocess.run([executables[label], "--bake-probes", scene], env=env, cwd=work, stdout=stream,
                               stderr=subprocess.STDOUT, check=True, timeout=180)
                elapsed = perf_counter()-start
            text = log.read_text(encoding="utf8", errors="replace")
            gpu = re.search(r"Vulkan device: (.+)", text)
            assert gpu and "Editor: Saved" in text and not re.search(r"VUID-|Validation Error|layer is not installed", text), text
            assert probes.is_file() and probes.stat().st_size > 0
            report["runs"].append(dict(pair=pair, build=label, seconds=elapsed, gpu=gpu[1].strip(),
                                       probe_sha256=digest(probes), bytes=probes.stat().st_size))
    report["summary"] = {}
    for label in executables:
        group = [r for r in report["runs"] if r["build"] == label]
        assert len({r["probe_sha256"] for r in group}) == 1
        times = [r["seconds"] for r in group if r["pair"] >= 0]
        deciles = quantiles(times, n=10, method="inclusive")
        report["summary"][label] = dict(median_seconds=median(times), p10_seconds=deciles[0], p90_seconds=deciles[-1])
    assert len({r["gpu"] for r in report["runs"]}) == 1
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(report, indent=2)+"\n", encoding="utf8")
    print(json.dumps(report["summary"], indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--first", type=Path, required=True)
    parser.add_argument("--revised", type=Path, required=True)
    parser.add_argument("--scene", type=Path, required=True, help="check_sky_viewport.py 准备的 Sky-Bunny.redflag")
    parser.add_argument("--out", type=Path, default=ROOT / "output/validation/sky-probes/bake-timing.json")
    args = parser.parse_args()
    measure(args.first.resolve(strict=True), args.revised.resolve(strict=True), args.scene.resolve(strict=True), args.out)
