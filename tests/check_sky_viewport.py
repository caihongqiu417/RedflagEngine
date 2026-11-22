"""从真实编辑器导出只受天光照亮的 Stanford Bunny，以及带烘焙光照的古董相机。"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

import yaml

from check_directional_viewport import ROOT, SCENE, capture
from profile_native_navigation import prepare


OUT = ROOT / "output/validation/sky-probes"


def render(exe, tools, label):
    work = ROOT / "tmp/sky-viewport" / label
    prepare(work)
    scene = work / "assets/scenes/Sky-Bunny.redflag"
    data = yaml.safe_load(SCENE.read_text(encoding="utf8"))
    data["Environment"] = "../textures/studio_small_09_1k.hdr"
    data["Entities"] = [e for e in data["Entities"] if "PointLightComponent" not in e and "DirectionalLightComponent" not in e]
    scene.write_text(yaml.safe_dump(data, allow_unicode=True), encoding="utf8")
    settings = dict(Shading="Lit", Direct="All point lights", Indirect="DDGI", TAA=True, GTAO=False,
                    SSR=False, SpecularIBL=False, FastNavigation=False, PreviewWidth=384)
    Path(str(scene) + ".render.yaml").write_text(yaml.safe_dump(settings), encoding="utf8")
    probes = Path(str(scene) + ".rfprobes")
    probes.unlink(missing_ok=True)
    binaries = ROOT / "bin/Debug-windows-x86_64/RedflagForge"
    env = dict(os.environ, PATH=str(binaries) + os.pathsep + os.environ["PATH"], RF_VULKAN_VALIDATION="1")
    with (work / "bake.log").open("wb") as log:
        subprocess.run([str(exe), "--bake-probes", str(scene)], cwd=work, env=env, stdout=log,
                       stderr=subprocess.STDOUT, timeout=180, check=True)
    assert probes.is_file()
    result = capture(exe, tools, scene, OUT / f"bunny-{label}.png")
    result.update(probes_sha256=hashlib.sha256(probes.read_bytes()).hexdigest(), settings=settings,
                  environment=data["Environment"], environment_sha256=hashlib.sha256(
                      (scene.parent / data["Environment"]).read_bytes()).hexdigest())
    assert (result["max_linear_rgb"] == 0) if label == "before" else (result["lit_pixels"] > 0)
    (OUT / f"viewport-{label}.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("label", choices=["before", "first", "after", "camera-studio"])
    parser.add_argument("exe", type=Path)
    parser.add_argument("--renderdoc", type=Path, default=ROOT / "tmp/tools/renderdoc/RenderDoc_1.47_64")
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    exe, tools = args.exe.resolve(strict=True), args.renderdoc.resolve(strict=True)
    if args.label == "camera-studio":
        scene = ROOT / "RedflagForge/assets/scenes/Camera-Studio.redflag"
        probes = Path(str(scene) + ".rfprobes").read_bytes()
        result = capture(exe, tools, scene, OUT / "camera-studio.png", width=640)
        result["probes_sha256"] = hashlib.sha256(probes).hexdigest()
        (OUT / "camera-studio.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf8")
    else:
        render(exe, tools, args.label)
