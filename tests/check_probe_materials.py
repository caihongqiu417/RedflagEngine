"""用编辑器烘焙同一间房，检查墙上换过的贴图会把什么颜色反射到地板上。"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

import numpy as np
from PIL import Image, ImageFilter
import yaml

from check_scene_reflections import ROOT, SCENES, WORK, digest
from profile_native_navigation import prepare


OUTPUT = ROOT / "output/profiling/probe-materials"
BIN = ROOT / "bin/Debug-windows-x86_64/RedflagForge"


def bake(exe, scene):
    probes = Path(str(scene) + ".rfprobes")
    probes.unlink(missing_ok=True)
    env = dict(os.environ, PATH=str(BIN) + os.pathsep + os.environ["PATH"], RF_VULKAN_VALIDATION="1")
    with scene.with_suffix(".log").open("wb") as log:
        subprocess.run([exe, "--bake-probes", scene], cwd=scene.parent, env=env,
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    return probes


def make_scene(folder, geometry, case):
    source = SCENES / "Light-Studio.redflag"
    room = yaml.safe_load(source.read_bytes())
    camera, _, wall, floor = room["Entities"]
    light = dict(Entity=5, TagComponent=dict(Tag="点光源"),
                 TransformComponent=dict(Translation=[0, 1.55, .48], Rotation=[0, 0, 0], Scale=[1, 1, 1]),
                 PointLightComponent=dict(Color=[1, 1, 1], Intensity=7, Radius=3, CastShadow=False))
    room["Entities"] = [camera, wall, floor, light]
    camera["TransformComponent"].update(Translation=[0, 1.15, 3.65], Rotation=[-.22, 0, 0])
    material = dict(BaseColor=[1, 1, 1, 1], Metallic=0, Roughness=1)
    if case in ("red", "blue"):
        material["BaseColorMap"] = (folder / (case + ".png")).as_posix()
    elif case == "red-factor":
        material["BaseColor"] = [1, 0, 0, 1]
    elif case in ("metal", "dielectric"):
        material.update(Metallic=1, MetallicRoughnessMap=(folder / (case + ".png")).as_posix())
    if geometry == "mesh":
        material["Color"] = [1, 1, 1, 1]
        wall["MeshRendererComponent"] = material
    else:
        del wall["MeshRendererComponent"]
        # 模型节点已经缩小一半，再用实体变换把墙调成与立方体墙相同的 5 × 2.5。
        wall["TransformComponent"].update(Translation=[0, 1.25, 0], Scale=[2.5, 1.25, 1])
        component = dict(FilePath=(folder / "quad.gltf").as_posix(), Color=[1, 1, 1, 1])
        if case == "clone":
            component["OverrideMaterials"] = [dict(BaseColor=[.1, .1, .1, 1], Metallic=.6, Roughness=.8,
                                                 ModelMaps=["BaseColorMap", "MetallicRoughnessMap"])]
        elif case != "original":
            component["OverrideMaterials"] = [material]
        wall["ModelRendererComponent"] = component
    scene = folder / f"{geometry}-{case}.redflag"
    scene.write_text(yaml.safe_dump(room, allow_unicode=True, sort_keys=False), encoding="utf8")
    Path(str(scene) + ".render.yaml").write_bytes(Path(str(source) + ".render.yaml").read_bytes())
    return scene


def measure(exe, geometry, folder, baseline):
    samples, cases = {}, {}
    names = ["red", "blue", "red-factor", "white", "metal", "dielectric"]
    if geometry == "model":
        names += ["original", "clone"]
    for case in names:
        scene = make_scene(folder, geometry, case)
        baked = bake(exe, scene)
        # 文件头后，每个方向依次存 RGB 光照和到遮挡物的距离统计。只比较光照。
        values = np.fromfile(baked, dtype="<f4", offset=24).reshape(-1, 2, 4)
        assert len(values) == 12 * 6 * 7 * 6 and np.isfinite(values).all()
        rgb = values[:, 0, :3]
        samples[case] = rgb
        cases[case] = dict(probe_sha256=digest(baked), mean_rgb=rgb.mean(axis=0, dtype=np.float64).tolist())
    if baseline:
        assert np.array_equal(samples["red"], samples["blue"])
        assert np.array_equal(samples["metal"], samples["dielectric"])
    else:
        assert cases["red"]["probe_sha256"] != cases["blue"]["probe_sha256"]
        assert np.max(np.abs(samples["red"] - samples["red-factor"])) < 1e-5
        assert np.max(np.abs(samples["red"] - samples["blue"][:, ::-1])) < 1e-5
        assert np.mean(samples["red"][:, 0] - samples["red"][:, 2]) > .02
        assert np.max(np.abs(samples["white"] - samples["dielectric"])) < 1e-5
        assert np.mean(samples["dielectric"] - samples["metal"]) > .02
        if geometry == "model":
            assert cases["original"]["probe_sha256"] == cases["clone"]["probe_sha256"]
    return cases


def run(exe, baseline):
    folder = WORK / ("probe-maps-" + digest(exe)[:12])
    prepare(folder)
    shutil.copyfile(ROOT / "tests/fixtures/TextureQuad.gltf", folder / "quad.gltf")
    for name, rgb in dict(red=(255, 0, 0), blue=(0, 0, 255), metal=(0, 255, 255), dielectric=(0, 255, 0)).items():
        Image.new("RGB", (2, 2), rgb).save(folder / (name + ".png"))
    return dict(executable_sha256=digest(exe), mesh=measure(exe, "mesh", folder, baseline),
                model=measure(exe, "model", folder, baseline))


def check_saved_rooms(exe):
    folder = WORK / ("saved-rooms-" + digest(exe)[:12])
    prepare(folder)
    cases = {}
    for name in ("Light-Studio", "Mirror-Studio", "RTXGI-Bathroom"):
        source = SCENES / (name + ".redflag")
        scene = folder / source.name
        for suffix in ("", ".render.yaml"):
            shutil.copyfile(str(source) + suffix, str(scene) + suffix)
        target = bake(exe, scene)
        original = Path(str(source) + ".rfprobes")
        assert original.read_bytes() == target.read_bytes(), name
        cases[name] = dict(scene_sha256=digest(source), source_probe_sha256=digest(original),
                           new_probe_sha256=digest(target), bytes=target.stat().st_size, identical=True)
    return dict(executable_sha256=digest(exe), cases=cases)


def capture_floor(exe, tools, baseline):
    stamp = digest(exe)[:12]
    source = WORK / ("probe-maps-" + stamp)
    cases, surfaces, colors = {}, [], []
    for case in ("red", "blue"):
        scene = source / ("model-" + case + ".redflag")
        subprocess.run([sys.executable, str(ROOT / "tests/capture_material_studio.py"),
                        "--exe", str(exe), "--renderdoc", str(tools), "--width", "480", "--scene", str(scene)],
                       cwd=ROOT, check=True)
        work = WORK / f"{scene.stem}-{stamp}-480"
        report = json.loads((work / "replay.json").read_bytes())
        w, h = report["size"]
        def pixels(name):
            return np.fromfile(work / (name + ".f32"), "<f4").reshape(h, w, 4)[:, :, :3]
        surfaces.append(pixels("surface"))
        colors.append(pixels("light"))
        display = np.uint8(np.clip(pixels("display"), 0, 1) * 255 + .5)
        label = "before" if baseline else "after"
        Image.fromarray(display).save(OUTPUT / (label + "-" + case + ".png"))
        path = Path((work / "frame-path.txt").read_text(encoding="utf8"))
        cases[case] = dict(file=path.relative_to(ROOT).as_posix(), sha256=digest(path),
                           frame=report["frame"], size=[w, h], display_event=report["display_event"])
    # 地板一直用 (0.65,0.65,0.65)。只统计两帧里都属于地板的像素，墙自己换色不会被计入。
    floor = np.max(np.abs(surfaces[0] - .65), axis=2) < 1e-4
    assert np.array_equal(floor, np.max(np.abs(surfaces[1] - .65), axis=2) < 1e-4)
    # TAA 每帧在像素内微调取样位置。避开墙、地板的接缝，保证读数始终取自地板。
    floor = np.asarray(Image.fromarray(floor.astype(np.uint8)).filter(ImageFilter.MinFilter(5))) != 0
    assert floor.sum() > 1000
    red, blue = [c[floor].mean(axis=0, dtype=np.float64) for c in colors]
    if baseline:
        assert np.max(np.abs(red - blue)) < 1e-5, (red, blue)
    else:
        assert red[0] > blue[0] and blue[2] > red[2], (red, blue)
    return dict(cases=cases, floor_pixels=int(floor.sum()), red_wall_floor_rgb=red.tolist(),
                blue_wall_floor_rgb=blue.tolist())


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=BIN / "RedflagForge.exe")
    parser.add_argument("--baseline", action="store_true", help="Reproduce the bug with the older executable")
    parser.add_argument("--renderdoc", type=Path, help="Also capture the red and blue rooms with the updated editor")
    parser.add_argument("--check-saved-rooms", action="store_true", help="Rebake temporary copies of the three supplied rooms")
    args = parser.parse_args()
    exe = args.exe.resolve(strict=True)
    OUTPUT.mkdir(parents=True, exist_ok=True)
    if args.check_saved_rooms:
        rooms = check_saved_rooms(exe)
        (OUTPUT / "bake-compat.json").write_text(json.dumps(rooms, indent=2) + "\n", encoding="utf8")
        sys.exit(0)
    result = run(exe, args.baseline)
    if args.renderdoc:
        result["viewport"] = capture_floor(exe, args.renderdoc.resolve(strict=True), args.baseline)
    label = "before" if args.baseline else "after"
    target = OUTPUT / (label + (".json" if args.renderdoc else "-bake-only.json"))
    target.write_text(json.dumps(result, indent=2) + "\n", encoding="utf8")
    print(target)
