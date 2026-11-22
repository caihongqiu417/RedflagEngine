"""用正式 Vulkan 编辑器验证灯牌、镜头外受光的白墙和 Sun Temple 的火盆。"""
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
SCENES = ROOT / "RedflagForge/assets/scenes"
WORK = ROOT / "tmp/material-reference"
OUTPUT = ROOT / "output/profiling/emissive-studio"


def make_scene(case):
    temple = case == "temple"
    source = SCENES / ("Sun-Temple.redflag" if temple else "Light-Studio.redflag")
    scene = yaml.safe_load(source.read_text(encoding="utf8"))
    entities = {e["TagComponent"]["Tag"]: e for e in scene["Entities"]}
    camera = entities["主相机"]
    if case == "baked":
        assert Path(str(source) + ".rfprobes").is_file(), "missing light baked for the supplied studio"
        return source, camera
    pose = camera["TransformComponent"]
    if case.startswith("wall"):
        pose.update(Translation=[.85, 1.15, 1.85], Rotation=[0, 0, 0])
        camera["CameraComponent"]["Camera"]["PerspectiveFov"] = .58
    if case.endswith("off"):
        # 关闭同一块材质的发光，保持原来的网格、底色、粗糙度和相机。
        entities["灯牌"]["ModelRendererComponent"]["OverrideMaterials"] = [
            dict(Emission=[0, 0, 0], BaseColor=[.08, .06, .05, 1], Roughness=.72, Metallic=0)]
    if temple:
        pose.update(Translation=[.05, 2.4, 70.25], Rotation=[-.427, -1.93, 0])
    directory = WORK / "emissive-scenes"
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / ("lamp-" + case + ".redflag")
    path.write_text(yaml.safe_dump(scene, allow_unicode=True, sort_keys=False), encoding="utf8")
    Path(str(path) + ".render.yaml").write_bytes(Path(str(source) + ".render.yaml").read_bytes())
    return path, camera


def project(point, camera, w, h):
    pose = camera["TransformComponent"]
    x, y, _ = pose["Rotation"]
    sx, cx, sy, cy = np.sin(x), np.cos(x), np.sin(y), np.cos(y)
    rotation = np.array([[cy, sy*sx, sy*cx], [0, cx, -sx], [-sy, cy*sx, cy*cx]])
    px, py, pz = rotation.T @ (np.array(point) - pose["Translation"])
    focal = h / (2 * np.tan(camera["CameraComponent"]["Camera"]["PerspectiveFov"] / 2))
    return np.array([w/2 + focal*px/(-pz), h/2 - focal*py/(-pz)]) - .5


def read_frame(directory, name):
    info = json.loads((directory / "replay.json").read_text(encoding="utf8"))
    w, h = info["size"]
    assert info["frame"] == 24 and not info["messages"]
    def image(file):
        rgba = np.fromfile(directory / (file + ".f32"), "<f4").reshape(h, w, 4)
        assert np.isfinite(rgba).all()
        return rgba[:, :, :3]
    pixels = image("light")
    display = np.uint8(np.clip(image("display"), 0, 1) * 255 + .5)
    Image.fromarray(display).save(OUTPUT / (name + ".png"))
    capture = Path((directory/"frame-path.txt").read_text(encoding="utf8"))
    with capture.open("rb") as data:
        checksum = hashlib.file_digest(data, "sha256").hexdigest()
    receipt = dict(size=[w,h], file=capture.relative_to(ROOT).as_posix(), sha256=checksum,
                   frame=24, display_event=info["display_event"])
    return pixels, receipt


def run(args, exe, case, name):
    scene, camera = make_scene(case)
    width = 800 if case == "temple" else 640
    with exe.open("rb") as binary:
        stamp = hashlib.file_digest(binary, "sha256").hexdigest()
    directory = WORK / f"{scene.stem}-{stamp[:12]}-{width}"
    if not args.measure_only:
        subprocess.run([sys.executable, str(ROOT/"tests/capture_material_studio.py"), "--exe", str(exe),
                        "--scene", str(scene), "--width", str(width), "--renderdoc", str(args.renderdoc)], check=True)
    rgb, receipt = read_frame(directory, name)
    receipt["executable_sha256"] = stamp
    if case != "temple":
        h, w = rgb.shape[:2]
        x, y = np.rint(project([.75, 1.2, 0], camera, w, h)).astype(int)
        receipt.update(wall_pixel=[int(x),int(y)], wall_rgb=rgb[y-2:y+3,x-2:x+3].mean(axis=(0,1),dtype=np.float64).tolist())
        if case.startswith("wall"):
            # 四个角都在相机左边才算灯牌完全出了画面，仍在场景中参与光照。
            for local_x in (-.64,.64):
                for local_y in (-.72,.72):
                    corner = [-.94 + np.cos(1.28)*local_x, 1.18 + local_y, .95 - np.sin(1.28)*local_x]
                    assert project(corner,camera,w,h)[0] < 0
            receipt["entire_panel_outside_view"] = True
    return rgb, receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--renderdoc", type=Path, required=True)
    parser.add_argument("--exe", type=Path, default=ROOT/"bin/Debug-windows-x86_64/RedflagForge/RedflagForge.exe")
    parser.add_argument("--previous", type=Path, help="Also compare Sun Temple with the executable before emission support")
    parser.add_argument("--measure-only", action="store_true", help="Read these executables' existing frame exports")
    args = parser.parse_args()
    OUTPUT.mkdir(parents=True, exist_ok=True)
    records = {}
    for case in ("wide", "wide-off", "wall", "wall-off", "temple"):
        pixels, records[case] = run(args, args.exe.resolve(), case, case)
        if case.endswith("off"): assert np.max(np.abs(pixels)) < 1e-5
        elif case != "temple": assert records[case]["wall_rgb"][0] > .01
    if args.previous:
        old, records["temple-before"] = run(args, args.previous.resolve(), "temple", "temple-before")
        red = pixels[:,:,0] - old[:,:,0]
        y,x = np.unravel_index(np.argmax(red), red.shape)
        assert red[y,x] > .1
        records["firepit"] = dict(pixel=[int(x),int(y)], before=old[y,x].tolist(), after=pixels[y,x].tolist(),
                                  pixels_red_gain_above_point_one=int(np.count_nonzero(red > .1)))
    _, records["baked"] = run(args, args.exe.resolve(), "baked", "baked")
    assert records["baked"]["wall_rgb"][0] > .01
    (OUTPUT/"frames.json").write_text(json.dumps(records, indent=2)+"\n",encoding="utf8")
    print("White wall with panel outside camera:",records["wall"]["wall_rgb"],"off:",records["wall-off"]["wall_rgb"])


if __name__ == "__main__":
    main()
