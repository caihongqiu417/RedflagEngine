"""比较两版实际 2×2 裁剪，在同一组相机位置绘制竖纹与斜纹。两个参数为已链接的测试程序副本。"""
import argparse
import base64
import json
import os
from pathlib import Path
import struct
import subprocess
import shutil

import numpy as np
from PIL import Image, ImageDraw, ImageFont

from check_fine_mask import prepare
from check_mask_supersampling import ROOT, PALETTES, SIZE, cameras, digest, frame_rgb, panel_rgb, picture, wire_area


OUT = ROOT / "output/validation/mask-footprint-compare"
WORK = ROOT / "tmp/mask-footprint-compare"


def orient(path, axis):
    obj = json.loads(path.read_bytes())
    header, encoded = obj["buffers"][0]["uri"].split(",")
    data = bytearray(base64.b64decode(encoded))
    primitive = obj["meshes"][0]["primitives"][0]
    uv = obj["accessors"][primitive["attributes"]["TEXCOORD_0"]]
    start = obj["bufferViews"][uv["bufferView"]].get("byteOffset", 0) + uv.get("byteOffset", 0)
    for i in range(uv["count"]):
        u,v = struct.unpack_from("<2f", data, start+8*i)
        struct.pack_into("<2f", data, start+8*i, .5 + axis[0]*(u-.5) + axis[1]*(v-.5), v)
    obj["buffers"][0]["uri"] = header + "," + base64.b64encode(data).decode()
    path.write_text(json.dumps(obj), encoding="utf8")


def diagonal_reference(rows):
    # H 的二阶导数就是每四列宽一列的二值条纹。一个像素同时跨过 U 的水平、垂直变化，取四角差得到面积。
    def primitive(t):
        n = np.floor(t/4)
        r = t-4*n
        return 2*n*n+1.5*n+n*r+np.where(r<=1,.5*r*r,r-.5)
    table = []
    edges = np.arange(SIZE+1)-SIZE/2
    for row in rows:
        w = float(row["texels_per_pixel"])
        x = np.clip(512*float(row["eye_x"]) + w*edges, -512, 512)
        y = np.clip(w*edges, -512, 512)
        h = primitive(512 + .8*x[None,:] + .6*y[:,None])
        table.append(np.diff(np.diff(h, axis=0), axis=1) / (.8*.6*w*w))
    ref = np.clip(np.array(table), 0, 1)
    # 独立密集取样核对近、中、远三处，其中远处检查了方片外轮廓。
    spot_checks = []
    sub = (np.arange(512)+.5)/512
    for r,j,i in ((0,31,32),(47,27,41),(len(rows)-1,16,12)):
        row = rows[r]
        w = float(row["texels_per_pixel"])
        x = 512*float(row["eye_x"])+(i-SIZE/2+sub[None,:])*w
        y = (j-SIZE/2+sub[:,None])*w
        samples = (np.abs(x)<=512) & (np.abs(y)<=512) & ((512+.8*x+.6*y)%4 < 1.)
        spot_checks.append(float(abs(samples.mean()-ref[r,j,i])))
    assert max(spot_checks) < .001, spot_checks
    return ref,spot_checks


def horizontal_reference(rows):
    table = []
    edges = np.arange(SIZE+1)-SIZE/2
    for row in rows:
        w = float(row["texels_per_pixel"])
        x = np.clip(512+512*float(row["eye_x"])+w*edges,0,1024)
        y = np.clip(512+w*edges,0,1024)
        solid = np.floor(y/4) + np.minimum(y%4,1)
        table.append(np.diff(solid)[:,None] * np.diff(x)[None,:]/(w*w))
    return np.array(table)


def gpu_frames(exe, name, geometry, rows, shader):
    work = WORK / geometry / name
    folder = prepare(work, False, True)
    if geometry != "vertical":
        orient(folder / "mask.gltf", (.8,.6) if geometry == "diagonal" else (0.,1.))
    (folder/"camera.txt").write_text("".join(f"{r['eye_x']} {r['eye_z']}\n" for r in rows), encoding="ascii")
    (folder/"resolve.spv").write_bytes(shader.read_bytes())
    env = dict(os.environ, RF_VULKAN_VALIDATION="1")
    env["PATH"] = str(ROOT/"bin/Debug-windows-x86_64/RedflagForge")+os.pathsep+env["PATH"]
    with (work/"renderer.log").open("wb") as log:
        subprocess.run([exe,folder], env=env, cwd=ROOT, stdout=log, stderr=log, check=True)
    return folder


def analyze_case(folder, rows, ref):
    tracks = np.array([r["track"] for r in rows])
    count = len(rows)
    fg,bg = PALETTES[0]
    color = frame_rgb(folder/"production-2-0.f32",count)
    a = (color[...,0]-bg[0])/(fg[0]-bg[0])
    info = {"mesh_sha256":digest(folder/"mask.gltf"), "shader_sha256":digest(folder/"resolve.spv"),
            "device":(folder/"gpu.txt").read_text().splitlines(), "tracks":{}, "tone_order_error":[]}
    for palette,(f,b) in enumerate(PALETTES):
        rgb = frame_rgb(folder/f"production-2-{palette}.f32", count)
        expected_rgb = a[...,None]*f + (1-a[...,None])*b
        assert np.array_equal(expected_rgb,rgb)
        displayed = frame_rgb(folder/f"production-2-{palette}-panel.f32",count)
        diff = float(np.abs(displayed-panel_rgb(rgb)).max())
        assert diff<1.e-5
        info["tone_order_error"].append(diff)
    for track in ("fixed","zoom","pan","far8","far26.5"):
        select=tracks==track
        actual,ideal=a[select],ref[select]
        error=actual-ideal
        info["tracks"][track] = dict(frames=int(select.sum()), spatial_mae=float(np.abs(error).mean()),
            temporal_mae=float(np.abs(np.diff(error,axis=0)).mean()), full_area=float(actual.mean()),
            expected_area=float(ideal.mean()), worst_pixel_error=float(np.abs(error).max()),
            missing_frames=int((actual.sum(axis=(1,2))==0).sum()))
    return info,np.array(color[:,SIZE//2])


def illustration(rows, saved):
    canvas=Image.new("RGB",(850,470),"#f8faf7")
    draw=ImageDraw.Draw(canvas)
    font=ImageFont.truetype("msyh.ttc",17)
    draw.text((12,8),"斜纹铁丝　每行是实际画面一帧的中央 64 像素，时间向下",font=font,fill="#273e43")
    for j,key in enumerate(("zoom","pan")):
        y=50+j*203
        draw.text((12,y),"相机后退" if key=="zoom" else "相机侧移",font=font,fill="#273e43")
        sel=np.array([r["track"]==key for r in rows])
        for i,(id,label) in enumerate((("first","初版 2×2"),("candidate","候选 2×2"),("ref","面积参考"))):
            img=picture(saved[id][sel]).resize((264,135), Image.Resampling.NEAREST)
            canvas.paste(img,(12+280*i,y+28))
            draw.text((12+280*i,y+166),label,font=font,fill="#273e43")
    canvas.save(OUT/"diagonal-motion.png")


def main():
    global WORK, OUT
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("first",type=Path)
    parser.add_argument("candidate",type=Path)
    parser.add_argument("--reuse",action="store_true",help="仅从已有 GPU 读回重新计算报告")
    parser.add_argument("--record-only",choices=("horizontal","vertical","diagonal"),help="只重画选定方向，其余轨迹复用已经读回的颜色")
    parser.add_argument("--run-name", default="current", help="独立保存本次试验，默认 current")
    args=parser.parse_args()
    if args.run_name:
        if not args.run_name.replace("-","").isalnum(): parser.error("run name must contain letters, digits, or dashes")
        OUT = OUT.with_name(OUT.name + "-" + args.run_name)
        WORK = WORK.with_name(WORK.name + "-" + args.run_name)
    OUT.mkdir(exist_ok=True, parents=True)
    WORK.mkdir(exist_ok=True, parents=True)
    rows=cameras()
    programs={"first":args.first.resolve(strict=True),"candidate":args.candidate.resolve(strict=True)}
    shader=WORK/"resolve.spv"
    if not args.reuse:
        compiler = shutil.which("glslc")
        if compiler is None:
            sdks = sorted(Path("C:/VulkanSDK").glob("*/Bin/glslc.exe"), reverse=True)
            compiler = Path(os.environ["VULKAN_SDK"])/"Bin/glslc.exe" if "VULKAN_SDK" in os.environ else sdks[0]
        subprocess.run([compiler,ROOT/"tests/fixtures/MaskLinearResolve.comp","-o",shader],check=True)
    report=dict(executable_sha256={k:digest(exe) for k,exe in programs.items()}, geometry={})
    fg,bg=PALETTES[0]
    for geometry in ("vertical","horizontal","diagonal"):
        if geometry=="diagonal": ref,report["diagonal_quadrature_errors"]=diagonal_reference(rows)
        elif geometry=="horizontal": ref=horizontal_reference(rows)
        else: ref=wire_area(rows)
        report["geometry"][geometry]={}
        saved={"ref":ref[:,SIZE//2,:,None]*fg+(1-ref[:,SIZE//2,:,None])*bg}
        for key,exe in programs.items():
            reuse = args.reuse or (args.record_only and args.record_only != geometry)
            folder=WORK/geometry/key/"bin-int/Tests/gi-mask" if reuse else gpu_frames(exe,key,geometry,rows,shader)
            info,saved[key]=analyze_case(folder,rows,ref)
            report["geometry"][geometry][key]=info
        if geometry=="diagonal": illustration(rows,saved)
    (OUT/"report.json").write_text(json.dumps(report, indent=2)+"\n",encoding="utf8")
    for geom,groups in report["geometry"].items():
        for key,g in groups.items():
            print(geom,key,[(t,round(m["spatial_mae"],5),round(m["temporal_mae"],5)) for t,m in g["tracks"].items()])


if __name__=="__main__":
    main()
