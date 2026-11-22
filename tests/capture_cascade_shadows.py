"""在游戏主相机下对照三层阴影的硬边、半影和关闭投影的画面。运行目录为仓库根。"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import sys

import numpy as np
from PIL import Image
import yaml


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "output/validation/cascaded-shadows"


def write_scene(path, casts, angle):
    studio = yaml.safe_load((ROOT / "RedflagForge/assets/scenes/Camera-Studio.redflag").read_text(encoding="utf8"))
    camera = next(e for e in studio["Entities"] if "CameraComponent" in e)
    camera["TransformComponent"].update(Translation=[0, 3.2, 8.4], Rotation=[-.32, 0, 0])
    camera["CameraComponent"]["Camera"]["PerspectiveFov"] = math.radians(48)
    entities = [camera]
    for i, (name, pos, size) in enumerate([
        ("地面", [0, -.10, -4], [18, .2, 22]),
        ("支墩", [-2, .7, 0], [1.3, 1.4, 1.3]),
        ("悬板", [0, 2.2, -2], [1.6, .15, 1.6]),
        ("远处立柱", [2, 2.25, -7], [.55, 4.5, .55]),
    ]):
        entities.append(dict(Entity=301+i, TagComponent=dict(Tag=name),
            TransformComponent=dict(Translation=pos, Rotation=[0,0,0], Scale=size),
            MeshRendererComponent=dict(Color=[1]*4, BaseColor=[.57,.56,.55,1], Metallic=0, Roughness=1)))
    entities.append(dict(Entity=311, TagComponent=dict(Tag="太阳"),
        TransformComponent=dict(Translation=[0,4,0], Rotation=[0,0,0], Scale=[1]*3),
        DirectionalLightComponent=dict(Direction=[.65,-1,.32], Color=[1,.94,.88], Intensity=3, CastShadow=casts)))
    scene = dict(Scene="级联阴影", SkyVisible=False, Entities=entities)
    path.write_text(yaml.safe_dump(scene, allow_unicode=True, sort_keys=False), encoding="utf8")
    settings = dict(Shading="Lit", Direct="All point lights", Indirect="Off", TAA=True,
        HistoryWeight=0, GTAO=False, SSR=False, SpecularIBL=False, ToneMapping="ACES",
        ShadowCascades=3, CascadeResolution=512, ShadowDistance=24, SunRadiusDegrees=angle)
    Path(str(path)+".render.yaml").write_text(yaml.safe_dump(settings), encoding="utf8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=ROOT/"bin/Debug-windows-x86_64/RedflagForge/RedflagForge.exe")
    parser.add_argument("--renderdoc", type=Path, default=ROOT/"tmp/tools/renderdoc/RenderDoc_1.47_64")
    parser.add_argument("--reuse", action="store_true")
    parser.add_argument("--output",type=Path,default=OUT)
    parser.add_argument("--record-baseline",action="store_true",help="旧版可保存未通过的地面样本结果")
    parser.add_argument("--time",action="store_true",help="预热一次，再测 11 次 GPU 回放事件")
    args = parser.parse_args()
    exe = args.exe.resolve(strict=True)
    version = hashlib.sha256(exe.read_bytes()).hexdigest()
    fixtures = ROOT/"tmp/cascaded-shadows"
    fixtures.mkdir(parents=True, exist_ok=True)
    args.output.mkdir(parents=True, exist_ok=True)
    captures, hdr = {}, {}
    for name, casts, angle in [("sun-off",False,0),("hard",True,0),("soft",True,4)]:
        scene = fixtures/("cascade-"+name+".redflag")
        write_scene(scene, casts, angle)
        work = ROOT/"tmp/material-reference"/f"{scene.stem}-{version[:12]}-600-f8"
        archived = work/"assets/scenes/Sponza-Atrium.redflag"
        expected_settings = yaml.safe_load(Path(str(scene)+".render.yaml").read_text(encoding="utf8"))
        expected_settings["PreviewWidth"] = 600
        reuse = (args.reuse and (work/"replay.json").exists() and archived.is_file()
            and archived.read_bytes() == scene.read_bytes() and Path(str(archived)+".render.yaml").exists()
            and yaml.safe_load(Path(str(archived)+".render.yaml").read_text(encoding="utf8")) == expected_settings)
        if not reuse:
            env = dict(os.environ, RF_STUDIO_GAME="assets/scenes/Sponza-Atrium.redflag")
            subprocess.run([sys.executable, str(ROOT/"tests/capture_material_studio.py"), "--exe", str(exe),
                "--scene", str(scene), "--width", "600", "--frame", "8", "--renderdoc", str(args.renderdoc)],
                cwd=ROOT, env=env, check=True)
        info = json.loads((work/"replay.json").read_text())
        w,h = info["size"]
        assert info["shading_size"] == [w,h] and not info["messages"]
        rgb = np.fromfile(work/"display.f32",np.float32).reshape(h,w,4)[:,:,:3]
        Image.fromarray(np.rint(np.clip(rgb,0,1)*255).astype(np.uint8)).save(args.output/(name+".png"))
        hdr[name] = np.fromfile(work/"light.f32",np.float32).reshape(h,w,4)[:,:,:3]
        rdc = Path((work/"frame-path.txt").read_text(encoding="utf8"))
        with rdc.open("rb") as stream:
            digest = hashlib.file_digest(stream, "sha256").hexdigest()
        captures[name] = dict(path=str(rdc.relative_to(ROOT)),sha256=digest,frame=info["frame"],size=[w,h],
            display_event=info["display_event"],surface_event=info["pack_event"])
    shading = np.max(hdr["sun-off"]-hdr["hard"],axis=2)
    penumbra = np.max(np.abs(hdr["soft"]-hdr["hard"]),axis=2)
    # 最下面五分之一只拍到无遮拦的平地，任何阴影变化都不是场景中的物体造成的。
    floor = np.s_[4*h//5:h,w//5:4*w//5]
    floor_error = (shading[floor] > .02).mean()
    result = dict(program_sha256=version,captures=captures,threshold_linear_rgb=.02,
        shadow_pixels=int((shading>.02).sum()),soft_edge_pixels=int((penumbra>.02).sum()),
        open_ground_samples=int(shading[floor].size),open_ground_changed_fraction=float(floor_error))
    assert result["shadow_pixels"] > 2000 and result["soft_edge_pixels"] > 300
    (args.output/"game.json").write_text(json.dumps(result,indent=2)+"\n", encoding="utf8")
    if not args.record_baseline:
        assert floor_error < .001, f"the open floor incorrectly received shadows in {floor_error:.1%} of samples"
    if args.time:
        from capture_material_studio import run_renderdoc
        frames = {name: ROOT/detail["path"] for name,detail in captures.items()}
        frames["three-maps"] = ROOT/"output/profiling/cascades/Shadow_capture.rdc"
        for name,rdc in frames.items():
            env = dict(os.environ, RF_CASCADE_TIMINGS=str((args.output/(name+"-time.json")).resolve()),
                RF_CASCADE_FRAME=str(rdc))
            run_renderdoc(args.renderdoc.resolve(),ROOT/"tests/replay_cascade_shadows.py",ROOT,env)
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
