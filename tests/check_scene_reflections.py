"""用 RenderDoc 对照镜面工作室和 RTXGI Bathroom 中的屏幕反射与场景反射。"""
import argparse
from copy import deepcopy
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

import numpy as np
from PIL import Image
import yaml

from capture_material_studio import run_renderdoc
from check_emissive_scene import project


ROOT = Path(__file__).resolve().parents[1]
SCENES = ROOT / "RedflagForge/assets/scenes"
WORK = ROOT / "tmp/material-reference"
OUTPUT = ROOT / "output/profiling/scene-reflections"


def digest(path):
    with path.open("rb") as data:
        return hashlib.file_digest(data, "sha256").hexdigest()


def prepare(case):
    studio = case.startswith("mirror")
    original = SCENES / ("Mirror-Studio.redflag" if studio else "RTXGI-Bathroom.redflag")
    scene = yaml.safe_load(original.read_text(encoding="utf8"))
    settings = yaml.safe_load(Path(str(original) + ".render.yaml").read_text(encoding="utf8"))
    settings.update(SSR=True, SceneReflections=not case.endswith("screen"), DenoiseReflections=False, TAA=True)
    directory = WORK / "reflection-scenes"
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / (case + ".redflag")
    probes = Path(str(path) + ".rfprobes")
    if case == "mirror-blocked":
        # 挡板放在镜面到灯牌的连线上。相机直接望向镜面的视线不会穿过它。
        board = deepcopy(next(e for e in scene["Entities"] if e["TagComponent"]["Tag"] == "镜面"))
        board["Entity"] = max(e["Entity"] for e in scene["Entities"]) + 1
        board["TagComponent"]["Tag"] = "挡板"
        board["TransformComponent"].update(Translation=[1.07, 1.53, 2.55], Scale=[1.12, 1.28, .12])
        board["MeshRendererComponent"].update(BaseColor=[.035, .40, .30, 1], Metallic=0, Roughness=.85)
        scene["Entities"].append(board)
    else:
        shutil.copyfile(str(original) + ".rfprobes", probes)
    path.write_text(yaml.safe_dump(scene, allow_unicode=True, sort_keys=False), encoding="utf8")
    Path(str(path) + ".render.yaml").write_text(yaml.safe_dump(settings), encoding="utf8")
    return path, scene


def sample_stripes(scene, image):
    entities = {e["TagComponent"]["Tag"]: e for e in scene["Entities"]}
    camera = entities["主相机"]
    cx, _, cz = camera["TransformComponent"]["Translation"]
    yaw = camera["TransformComponent"]["Rotation"][1]
    x, y, z = entities["灯牌"]["TransformComponent"]["Translation"]
    h, w = image.shape[:2]
    # 镜面在 z=0。灯牌四个角都在相机后面，镜中的虚像却在 z=-4.8。
    assert all(-(sx-cx)*np.sin(yaw) - (z-cz)*np.cos(yaw) < 0 for sx in (x-.64, x+.64))
    patches = {}
    for name, offset_y in (("white", .48), ("gray", 0), ("black", .18)):
        px, py = np.rint(project([x, y+offset_y, -z], camera, w, h)).astype(int)
        patch = image[py-1:py+2, px-1:px+2]
        assert patch.shape == (3, 3, 4)
        patches[name] = dict(pixel=[int(px), int(py)], rgb=np.median(patch[:, :, :3], axis=(0, 1)).tolist(),
                             hit_pixels=int(np.count_nonzero(patch[:, :, 3] > .5)))
    return patches


def run(case, args, stamp):
    path, scene = prepare(case)
    width = 640 if case.startswith("mirror") else 320
    work = WORK / f"{path.stem}-{stamp[:12]}-{width}"
    env = dict(os.environ, RF_MEASURE_REFLECTIONS="1")
    if not args.replay:
        if case == "mirror-blocked":
            binaries = ROOT / "bin/Debug-windows-x86_64/RedflagForge"
            bake_env = dict(env, PATH=str(binaries) + os.pathsep + env["PATH"])
            subprocess.run([str(args.exe), "--bake-probes", str(path)], cwd=ROOT / "RedflagForge",
                           env=bake_env, check=True)
        subprocess.run([sys.executable, str(ROOT / "tests/capture_material_studio.py"), "--exe", str(args.exe),
                        "--renderdoc", str(args.renderdoc), "--scene", str(path), "--width", str(width)],
                       cwd=ROOT, env=env, check=True)
    else:
        recorded = Path((work / "frame-path.txt").read_text(encoding="utf8"))
        env.update(RF_STUDIO_CAPTURE=str(recorded), RF_STUDIO_EXPORT=str(work))
        run_renderdoc(args.renderdoc, ROOT / "tests/replay_material_studio.py", ROOT, env)
    info = json.loads((work / "replay.json").read_bytes())
    assert info["frame"] == 24 and not info["messages"] and info["reflection"]["scene_trace"] == (not case.endswith("screen")), info
    entry = work / "assets/scenes/Sponza-Atrium.redflag"
    assert entry.read_bytes() == path.read_bytes(), "Captured scene differs from the current example"
    expected = yaml.safe_load(Path(str(path) + ".render.yaml").read_bytes())
    expected["PreviewWidth"] = width
    assert yaml.safe_load(Path(str(entry) + ".render.yaml").read_bytes()) == expected
    assert digest(Path(str(entry) + ".rfprobes")) == digest(Path(str(path) + ".rfprobes"))
    w, h = info["size"]
    def read(name):
        rgba = np.fromfile(work / (name + ".f32"), "<f4").reshape(h, w, 4)
        assert np.isfinite(rgba).all(), name
        return rgba
    display = read("display")[:, :, :3]
    Image.fromarray(np.uint8(np.clip(display, 0, 1)*255+.5)).save(OUTPUT / (case + ".png"))
    reflection = read("reflection-out")
    reflection[:, :, :3] -= read("reflection-in")[:, :, :3]
    capture = Path((work / "frame-path.txt").read_text(encoding="utf8"))
    info.update(capture=capture.relative_to(ROOT).as_posix(), capture_sha256=digest(capture),
                reflection_hit_pixels=int(np.count_nonzero(reflection[:, :, 3] > .5)),
                scene_sha256=digest(entry), settings_sha256=digest(Path(str(entry) + ".render.yaml")),
                baked_probes_sha256=digest(Path(str(entry) + ".rfprobes")))
    if case.startswith("mirror"):
        info["stripes"] = sample_stripes(scene, reflection)
    return info


def compare_mirror_edge(args, stamp):
    # 两版都用粗糙度为零的同一面镜子，并从同一份烘焙文件取光。
    path, scene = prepare("mirror-before")
    old_stamp = digest(args.previous)
    old = WORK / f"{path.stem}-{old_stamp[:12]}-640"
    new = WORK / f"mirror-scene-{stamp[:12]}-640"
    if not args.replay:
        subprocess.run([sys.executable, str(ROOT / "tests/capture_material_studio.py"),
                        "--exe", str(args.previous), "--renderdoc", str(args.renderdoc),
                        "--scene", str(path), "--width", "640"], cwd=ROOT,
                       env=dict(os.environ, RF_MEASURE_REFLECTIONS="1"), check=True)
    assert digest(old / "reflection-in.f32") == digest(new / "reflection-in.f32")
    info = json.loads((old / "replay.json").read_bytes())
    w, h = info["size"]
    entities = {e["TagComponent"]["Tag"]: e for e in scene["Entities"]}
    camera = entities["主相机"]
    x, y, z = entities["灯牌"]["TransformComponent"]["Translation"]
    corners = np.array([project([x + dx, y + dy, -z], camera, w, h)
                        for dx in (-.64, .64) for dy in (-.72, .72)])
    # 只数外框横向之外的像素，两侧各取 8 列。与投影边缘隔开 2 像素，留出 TAA 和双线性采样的宽度。
    left, top = np.floor(corners.min(axis=0)).astype(int)
    right, bottom = np.ceil(corners.max(axis=0)).astype(int)
    def read_light(work):
        linear = np.fromfile(work / "reflection-out.f32", "<f4").reshape(h, w, 4)
        linear -= np.fromfile(work / "reflection-in.f32", "<f4").reshape(h, w, 4)
        return linear
    # 中间灰条明显偏红。取它红绿差的一半，两次测量都用这条线来判定橙色亮点。
    gray = sample_stripes(scene, read_light(new))["gray"]["rgb"]
    cutoff = (gray[0] - gray[1]) * .5
    def count(work, label):
        linear = read_light(work)
        strips = np.concatenate([linear[top:bottom, left-10:left-2], linear[top:bottom, right+3:right+11]], axis=1)
        color = np.fromfile(work / "display.f32", "<f4").reshape(h, w, 4)[:, :, :3]
        crop = color[top-12:bottom+12, left-12:right+12]
        Image.fromarray(np.uint8(np.clip(crop, 0, 1)*255+.5)).save(OUTPUT / ("mirror-edge-" + label + ".png"))
        return dict(pixels=int(strips.shape[0]*strips.shape[1]), cutoff=cutoff,
                    orange_pixels=int(np.count_nonzero(strips[:, :, 0] - strips[:, :, 1] > cutoff)))
    before, after = count(old, "before"), count(new, "after")
    capture = Path((old / "frame-path.txt").read_text(encoding="utf8"))
    return dict(executable_sha256=old_stamp, capture=capture.relative_to(ROOT).as_posix(),
                capture_sha256=digest(capture), frame=info["frame"], event=info["reflection"]["event"],
                bounds=[int(left), int(top), int(right), int(bottom)], before=before, after=after)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=ROOT / "bin/Debug-windows-x86_64/RedflagForge/RedflagForge.exe")
    parser.add_argument("--renderdoc", type=Path, default=ROOT / "tmp/tools/renderdoc/RenderDoc_1.47_64")
    parser.add_argument("--previous", type=Path, help="Hybrid reflection executable before exact smooth mirrors")
    parser.add_argument("--replay", action="store_true", help="Remeasure the recorded frame for this executable and case")
    args = parser.parse_args()
    args.exe = args.exe.resolve(strict=True)
    args.renderdoc = args.renderdoc.resolve(strict=True)
    stamp = digest(args.exe)
    OUTPUT.mkdir(parents=True, exist_ok=True)
    cases = {name: run(name, args, stamp) for name in
             ("mirror-screen", "mirror-scene", "mirror-blocked", "bathroom-screen", "bathroom-scene")}
    screen, mirror, wall = [cases[name]["stripes"] for name in ("mirror-screen", "mirror-scene", "mirror-blocked")]
    assert all(s["hit_pixels"] == 0 for s in screen.values()) and all(s["hit_pixels"] == 9 for s in mirror.values())
    white, gray, black = [mirror[name]["rgb"][0] for name in ("white", "gray", "black")]
    # 贴图的中条是 8 位 sRGB 的 128，转成线性亮度约为白条的 21.6%。扣掉不发光处反射的光再比较。
    ratio = (gray-black)/(white-black)
    assert white > 1 and .15 < ratio < .28, (white, gray, black)
    assert wall["white"]["hit_pixels"] == 9 and wall["white"]["rgb"][0] < white * .2, wall
    assert wall["white"]["rgb"][1] > wall["white"]["rgb"][0], wall
    assert cases["bathroom-scene"]["reflection_hit_pixels"] > cases["bathroom-screen"]["reflection_hit_pixels"]
    report = dict(executable_sha256=stamp, stripe_ratio=ratio, cases=cases)
    if args.previous:
        args.previous = args.previous.resolve(strict=True)
        report["mirror_edge"] = compare_mirror_edge(args, stamp)
    (OUTPUT / "frames.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf8")
    print("Measured middle / white stripe:", ratio)


if __name__ == "__main__":
    main()
