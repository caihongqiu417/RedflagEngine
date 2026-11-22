"""用修改前后的正式编辑器打开同一只 Stanford Bunny，回放 RenderDoc 第 24 帧。"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

import numpy as np
from PIL import Image
import yaml


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "output/validation/directional-light"
SCENE = ROOT / "RedflagForge/assets/scenes/Reference-StanfordBunny.redflag"


def capture(exe, tools, scene, label):
    with exe.open("rb") as binary:
        digest = hashlib.file_digest(binary, "sha256").hexdigest()
    subprocess.run([sys.executable, str(ROOT / "tests/capture_material_studio.py"), "--renderdoc", str(tools),
                    "--exe", str(exe), "--scene", str(scene), "--width", "384"], cwd=ROOT, check=True)
    work = ROOT / "tmp/material-reference" / f"{scene.stem}-{digest[:12]}-384"
    replay = json.loads((work / "replay.json").read_text(encoding="utf8"))
    w, h = replay["size"]
    image = np.fromfile(work / "display.f32", "<f4").reshape(h, w, 4)[:, :, :3]
    light = np.fromfile(work / "light.f32", "<f4").reshape(h, w, 4)[:, :, :3]
    assert replay["pack_event"] and replay["frame"] == 24 and not replay["messages"]
    assert np.isfinite(image).all() and np.isfinite(light).all()
    Image.fromarray(np.uint8(np.clip(image, 0, 1) * 255 + .5)).save(OUT / f"bunny-{label}.png")
    return dict(executable_sha256=digest, renderdoc=replay, scene_sha256=hashlib.sha256(scene.read_bytes()).hexdigest(),
                capture=Path((work / "frame-path.txt").read_text(encoding="utf8")).name,
                max_linear_rgb=float(light.max()), lit_pixels=int((light.max(axis=2) > .01).sum()))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before-exe", type=Path, required=True)
    parser.add_argument("--exe", type=Path, default=ROOT / "bin/Debug-windows-x86_64/RedflagForge/RedflagForge.exe")
    parser.add_argument("--renderdoc", type=Path, default=ROOT / "tmp/tools/renderdoc/RenderDoc_1.47_64")
    args = parser.parse_args()
    scene = ROOT / "tmp/directional-review/Sun-Bunny.redflag"
    scene.parent.mkdir(parents=True, exist_ok=True)
    OUT.mkdir(parents=True, exist_ok=True)
    data = yaml.safe_load(SCENE.read_text(encoding="utf8"))
    data["Entities"] = [e for e in data["Entities"] if "PointLightComponent" not in e]
    scene.write_text(yaml.safe_dump(data, allow_unicode=True), encoding="utf8")
    settings = dict(Shading="Lit", Direct="All point lights", Indirect="Off", TAA=True, GTAO=False,
                    SSR=False, SpecularIBL=False, FastNavigation=False, PreviewWidth=384)
    Path(str(scene) + ".render.yaml").write_text(yaml.safe_dump(settings), encoding="utf8")
    before = capture(args.before_exe.resolve(), args.renderdoc.resolve(), scene, "before")
    after = capture(args.exe.resolve(), args.renderdoc.resolve(), scene, "after")
    assert before["max_linear_rgb"] == 0 and after["lit_pixels"] > 0
    report = dict(scene=SCENE.name, settings=settings, threshold_linear_rgb=.01, before=before, after=after)
    (OUT / "viewport.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf8")
