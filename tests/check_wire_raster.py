"""用编辑器的 Vulkan 光栅器画 Sponza 原网格。项目根目录运行 python tests/check_wire_raster.py，可加 --renderdoc DLL路径。"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

import numpy as np
from PIL import Image, ImageDraw, ImageFont

from check_wire_scale import render
from project_atrium_wires import ROOT, SCENE, project, rotation, scene_geometry


def save_animation(path, samples):
    font = ImageFont.truetype(str(Path(os.environ["WINDIR"])/"Fonts/msyh.ttc"),19)
    frames = []
    for i in range(17):
        panel = Image.new("RGB",(1060,257),"#f7f6f2")
        draw = ImageDraw.Draw(panel)
        draw.text((14,9),f"Sponza 吊绳   ·   相机侧移第 {i:02d} 步   ·   灰度表示绳所占的面积",font=font,fill="#26343a")
        for column,(name,seq) in enumerate(samples.items()):
            x = 14+261*column
            draw.text((x,39),name,font=font,fill="#33454e")
            grey = Image.fromarray(np.uint8(seq[i]*255+.5))
            panel.paste(grey.resize((252,189),Image.Resampling.NEAREST),(x,66))
        frames.append(panel)
    frames[0].save(path,save_all=True,append_images=frames[1:]+frames[-2:0:-1],
                   duration=110,loop=0,optimize=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--renderdoc", type=Path)
    args = parser.parse_args()
    work = ROOT / "tmp/wire-raster-pan"
    out = ROOT / "output/validation/atrium-aa"
    work.mkdir(parents=True, exist_ok=True)
    out.mkdir(parents=True, exist_ok=True)
    size = (600,385)
    bounds = x0,x1,y0,y1 = (258,342,36,99)
    width, height = x1-x0, y1-y0
    camera, vertices, indices = scene_geometry(SCENE)
    pose = camera["TransformComponent"]
    eye = np.array(pose["Translation"])
    forward = -rotation(pose["Rotation"])[:,2]
    lens = camera["CameraComponent"]["Camera"]
    with (work / "wire.bin").open("wb") as file:
        np.array([len(vertices),indices.size,*size,x0,y0,width,height],"<u4").tofile(file)
        np.array([*eye,*forward,lens["PerspectiveFov"],lens["PerspectiveNear"],lens["PerspectiveFar"]],"<f4").tofile(file)
        vertices.astype("<f4").tofile(file)
        indices.astype("<u4").tofile(file)

    cmd = [str(ROOT / "tests/run_taa_camera_tests.cmd"),str(work)]
    if args.renderdoc:
        cmd.append(str(args.renderdoc.resolve(strict=True)))
    log = subprocess.run(cmd, cwd=ROOT, capture_output=True, env=dict(os.environ, RF_WIRE_RASTER="1"))
    (work / "run.log").write_bytes(log.stdout + log.stderr)
    if log.returncode:
        raise RuntimeError((log.stdout+log.stderr).decode(errors="replace"))
    # 与 C++ 相机走同一条路线，各像素另用 32×32 次几何采样计算参考面积。
    poses = [project(*size, SCENE, eye+[0,0,i*.0055]) for i in range(17)]
    reference = np.array([render(tri,*size,32,bounds) for tri in poses])
    cases, samples = {}, {}
    for side in (1,2,4):
        data = np.fromfile(work / f"coverage-{side}.bin",np.uint8)
        rendered = data.reshape(17,height,side,width,side).mean((2,4))
        samples[f"GPU   {side} × {side}"] = rendered
        cases[str(side)] = {}
        for label, rows in [("near",slice(0,29)),("far",slice(29,None))]:
            actual, ideal = rendered[:,rows], reference[:,rows]
            edge = (ideal>0) & (ideal<1)
            delta = actual - ideal
            ratio = actual.sum((1,2))/ideal.sum((1,2))
            cases[str(side)][label] = dict(edge_mae=float(abs(delta[edge]).mean()),
                motion_delta_mae=float(abs(np.diff(delta,axis=0)[edge[:-1]|edge[1:]]).mean()),
                area_ratio_min=float(ratio.min()), area_ratio_max=float(ratio.max()))
    for label in ("near","far"):
        for metric in ("edge_mae","motion_delta_mae"):
            assert cases["1"][label][metric] > cases["2"][label][metric] > cases["4"][label][metric], cases
    samples["几何参考   32 × 32"] = reference
    save_animation(out / "gpu-pan.gif",samples)
    report = dict(scene=SCENE.name, mesh_index=0, primitive=52, size=size, bounds_xyxy=[x0,y0,x1,y1],
                  frame_count=17, camera_step=[0,0,.0055], reference_per_axis=32, jitter=False,
                  color="unlit white wire, black background; VulkanDraw::DrawSurfaces read back before any filtering.",
                  scene_sha256=hashlib.sha256(SCENE.read_bytes()).hexdigest(), cases=cases)
    if args.renderdoc:
        capture = max(work.glob("Wire-Raster*.rdc"),key=lambda p:p.stat().st_mtime_ns)
        destination = ROOT / "output/captures/local/wire-raster-pan" / capture.name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(capture, destination)
        report["capture"] = destination.relative_to(ROOT).as_posix()
        report["capture_sha256"] = hashlib.sha256(destination.read_bytes()).hexdigest()
    (out / "gpu-pan-coverage.json").write_text(json.dumps(report,indent=2)+"\n",encoding="utf8")
    print(json.dumps(report,indent=2))


if __name__ == "__main__":
    main()
