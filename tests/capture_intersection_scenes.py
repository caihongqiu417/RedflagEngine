"""从项目根目录抓取 Sponza 与 Sun Temple 的直接光、实时探针和体积雾。"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

import yaml

from capture_material_studio import run_renderdoc


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--renderdoc", type=Path, default=root / "tmp/tools/renderdoc/RenderDoc_1.47_64")
    parser.add_argument("--scene", choices=("Sun-Temple", "Sponza-Atrium"), action="append",
                        help="Defaults to both scenes")
    parser.add_argument("--reuse", action="store_true", help="Reuse frame 24 for this executable and scene")
    parser.add_argument("--measure", action="store_true", help="Warm up once and record 11 GPU timings per stage")
    args = parser.parse_args()
    exe = args.exe.resolve(strict=True)
    renderdoc = args.renderdoc.resolve(strict=True)
    with exe.open("rb") as binary:
        version = hashlib.file_digest(binary, "sha256").hexdigest()[:12]
    work = root / "tmp/intersection-scenes"
    work.mkdir(parents=True, exist_ok=True)
    for label in args.scene or ("Sun-Temple", "Sponza-Atrium"):
        scene = root / "RedflagForge/assets/scenes" / (label + ".redflag")
        entry = work / (label + "-rays.redflag")
        shutil.copyfile(scene, entry)
        settings = yaml.safe_load(Path(str(scene) + ".render.yaml").read_text(encoding="utf8"))
        settings.update(Shading="Lit", PreviewWidth=320, Direct="All point lights", Indirect="DDGI",
                        Samples=2, TAA=False, GTAO=False, SSR=False, FastNavigation=False,
                        VolumetricFog=True, FogDensity=.06, FogAlbedo=.8, FogAnisotropy=.15, FogDistance=12, FogSteps=8)
        Path(str(entry) + ".render.yaml").write_text(yaml.safe_dump(settings), encoding="utf8")
        case = root / "tmp/material-reference" / f"{entry.stem}-{version}-320"
        previous = case / "assets/scenes/Sponza-Atrium.redflag"
        frame_path = case / "frame-path.txt"
        reused = args.reuse and frame_path.is_file() and Path(frame_path.read_text(encoding="utf8")).is_file()
        if reused:
            if previous.read_bytes() != entry.read_bytes() or settings != yaml.safe_load(
                    Path(str(previous) + ".render.yaml").read_text(encoding="utf8")):
                raise ValueError(f"The saved capture uses a different scene or settings: {case}")
        else:
            subprocess.run([sys.executable, str(root / "tests/capture_material_studio.py"), "--exe", str(exe),
                            "--scene", str(entry), "--width", "320", "--renderdoc", str(renderdoc)], check=True)
        report = case / "intersection.json"
        if reused and not args.measure and report.is_file():
            print(report)
            continue
        error = case / "intersection-error.txt"
        error.unlink(missing_ok=True)
        before = report.stat().st_mtime_ns if report.is_file() else 0
        env = dict(os.environ, RF_RAY_WORK=str(case), RF_RAY_TIMINGS="1" if args.measure else "0")
        run_renderdoc(renderdoc, root / "tests/replay_intersection_scenes.py", root, env)
        if error.is_file():
            raise RuntimeError(error.read_text(encoding="utf8"))
        assert report.stat().st_mtime_ns > before
        result = json.loads(report.read_text(encoding="utf8"))
        assert set(result["stages"]) == {"direct", "probe", "ddgi", "fog"}, result["stages"]
        for stage, row in result["stages"].items():
            if args.measure:
                assert len(row["gpu_ms"]) == 11
                print(f"{label} {stage}: {row['median_ms']:.4f} ms")
        print(report)


if __name__ == "__main__":
    main()
