"""从仓库根目录运行。在同一台相机前，只切换维修棚工作灯。"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys

import numpy as np
from PIL import Image
import yaml

ROOT = Path(__file__).resolve().parents[1]
SCENE = ROOT / "RedflagForge/assets/scenes/Watt-Station.redflag"
OUT = ROOT / "output/validation/watt-station/workshop-light"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=ROOT / "bin/Debug-windows-x86_64/RedflagForge/RedflagForge.exe")
    parser.add_argument("--renderdoc", type=Path, default=ROOT / "tmp/tools/renderdoc/RenderDoc_1.47_64")
    args = parser.parse_args()
    exe = args.exe.resolve(strict=True)
    version = hashlib.sha256(exe.read_bytes()).hexdigest()
    source = SCENE.read_bytes()
    settings = Path(str(SCENE) + ".render.yaml").read_bytes()
    data = yaml.safe_load(source)
    camera, target = np.array([2., 1.65, 1.9]), np.array([7.2, 1.15, 1.2])
    direction = target - camera
    pitch = math.asin(direction[1] / np.linalg.norm(direction))
    yaw = math.atan2(-direction[0], -direction[2])
    main_camera = next(e for e in data["Entities"] if e.get("CameraComponent", {}).get("Primary"))
    main_camera["TransformComponent"].update(Translation=camera.tolist(), Rotation=[pitch, yaw, 0.])
    for entity in data["Entities"]:
        if "ModelAnimationComponent" in entity:
            entity["ModelAnimationComponent"].update(Playing=False, Time=0.)
    lamp = next(e for e in data["Entities"] if e["Entity"] == 1223)["PointLightComponent"]
    intensity = lamp["Intensity"]
    report = dict(scene_sha256=hashlib.sha256(source).hexdigest(), settings_sha256=hashlib.sha256(settings).hexdigest(),
                  build_sha256=version, camera=camera.tolist(), target=target.tolist(), lamp=1223, intensity=intensity,
                  color=lamp["Color"], radius=lamp["Radius"], pose_seconds=0)
    work = ROOT / "tmp/workshop-light"
    work.mkdir(parents=True, exist_ok=True)
    OUT.mkdir(parents=True, exist_ok=True)
    frames = {}
    for case, power in (("on", intensity), ("off", 0)):
        scene = work / f"workshop-lamp-{case}.redflag"
        lamp["Intensity"] = power
        scene.write_text(yaml.safe_dump(data, allow_unicode=True, sort_keys=False), encoding="utf8")
        Path(str(scene) + ".render.yaml").write_bytes(settings)
        subprocess.run([sys.executable, str(ROOT / "tests/capture_material_studio.py"), "--exe", str(exe),
                        "--renderdoc", str(args.renderdoc), "--width", "960", "--frame", "24", "--scene", str(scene)], check=True)
        capture = ROOT / "tmp/material-reference" / f"{scene.stem}-{version[:12]}-960"
        replay = json.loads((capture / "replay.json").read_text())
        width, height = replay["size"]
        def rgb(name):
            return np.fromfile(capture / (name + ".f32"), np.float32).reshape(height, width, 4)[..., :3]
        frames[case] = (rgb("light"), rgb("surface"))
        display = np.rint(255 * np.clip(rgb("display"), 0, 1)).astype(np.uint8)
        Image.fromarray(display).save(OUT / (case + ".png"))
        frame = Path((capture / "frame-path.txt").read_text().strip())
        report[case] = dict(frame=frame.relative_to(ROOT).as_posix(), sha256=hashlib.file_digest(frame.open("rb"), "sha256").hexdigest(),
                            size=replay["size"], renderdoc=replay["tool"], frame_number=replay["frame"],
                            exposure_multiplier=replay["exposure_multiplier"], tone_mapping=replay["tone_mapping"],
                            display_event=replay["display_event"], pack_event=replay["pack_event"])

    (lit, albedo), (unlit, other_albedo) = frames["on"], frames["off"]
    assert np.array_equal(albedo, other_albedo), "摄像机或场景几何发生了额外变化"
    y = np.array([.2126, .7152, .0722])
    x1, x2, y1, y2 = width // 4, 3 * width // 4, height // 4, 3 * height // 4
    report["center"] = dict(x=[x1,x2], y=[y1,y2], pixels=(x2-x1)*(y2-y1))
    for case, hdr in (("on",lit), ("off",unlit)):
        report["center"][case + "_rgb"] = hdr[y1:y2,x1:x2].mean((0,1), dtype=np.float64).tolist()
        report["center"][case + "_linear_luminance"] = float((hdr[y1:y2,x1:x2] @ y).mean())
    report["identical_surface_pixels"] = width * height
    assert report["center"]["on_linear_luminance"] > report["center"]["off_linear_luminance"], report["center"]
    (OUT / "measurement.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf8")
    print(report["center"])


if __name__ == "__main__":
    main()
