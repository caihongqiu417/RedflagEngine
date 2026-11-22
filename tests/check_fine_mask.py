"""用现有 GIMaskTests.exe 检查 1024 像素宽的透明图，只改变模型与图片，不改着色器。"""
import argparse
import base64
import csv
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "output/validation/fine-mask"
SIZE = 64


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def prepare(run, clear, distant):
    run.mkdir(exist_ok=True, parents=True)
    subprocess.run([sys.executable, ROOT / "tests/prepare_gi_mask.py"], cwd=run, check=True)
    files = run / "bin-int/Tests/gi-mask"
    # 每四列有一列铁丝。旧导入只取原图的奇数列，会把所有偶数列上的铁丝漏掉。
    wire = np.zeros((32, 1024), dtype=np.uint8)
    if not clear:
        wire[:, ::4] = 255
    base = np.empty((32, 1024, 4), dtype=np.uint8)
    base[:, :, :3] = (42, 180, 68)
    base[:, :, 3] = wire
    Image.fromarray(base).save(files / "mask.png")
    red = np.empty_like(base)
    red[:, :, 0] = wire
    red[:, :, 1:] = (255, 0, 255)
    Image.fromarray(red).save(files / "opacity.png")

    # 摄像机覆盖纹理中央的 12 列。网格在画面上约宽 38.6 像素，1 列铁丝占约 3.2 个画面像素。
    u0, u1 = (0., 1.) if distant else (512.125 / 1024, 524.125 / 1024)
    for name in ("opaque", "mask"):
        entry = files / (name + ".gltf")
        model = json.loads(entry.read_text(encoding="utf8"))
        header, encoded = model["buffers"][0]["uri"].split(",")
        data = bytearray(base64.b64decode(encoded))
        offset = model["bufferViews"][1]["byteOffset"]
        for vertex in range(4):
            u, v = struct.unpack_from("<2f", data, offset + vertex * 8)
            struct.pack_into("<2f", data, offset + vertex * 8, u0 + u * (u1-u0), v)
        model["buffers"][0]["uri"] = header + "," + base64.b64encode(data).decode("ascii")
        entry.write_text(json.dumps(model), encoding="utf8")
    obj = files / "fence.obj"
    obj.write_text(obj.read_text().replace("vt 0 ", f"vt {u0} ").replace("vt 1 ", f"vt {u1} "), encoding="ascii")
    return files


def gpu_image(files, case, stage):
    fields = 4 if stage == "shadow" else 12
    return np.fromfile(files / f"{case}-{stage}.bin", dtype="<f4").reshape(SIZE, SIZE, fields)


def export(name, data):
    # 所有版本使用同一亮度范围，按整数倍放大方便数出一根线占了几列。
    image = Image.fromarray(np.uint8(np.clip(data, 0, 1) * 255 + .5))
    image.resize((SIZE*3, SIZE*3), Image.Resampling.NEAREST).save(OUT / (name + ".png"))


def run(exe, label, clear, renderdoc, references=None):
    work = ROOT / "tmp/fine-mask" / label / ("clear" if clear else "wires")
    files = prepare(work, clear, label.startswith("far-"))
    for key, path in (references or {}).items():
        (files / (key + ".rfprobes")).write_bytes(path.read_bytes())
    env = dict(os.environ, RF_VULKAN_VALIDATION="1")
    env["PATH"] = str(ROOT / "bin/Debug-windows-x86_64/RedflagForge") + os.pathsep + env["PATH"]
    command = [str(exe)]
    if renderdoc and not clear:
        library = renderdoc.resolve(strict=True)
        env.update(VK_ADD_LAYER_PATH=str(library.parent), VK_INSTANCE_LAYERS="VK_LAYER_RENDERDOC_Capture")
        command.extend([str(library), str(OUT/label)])
    with (work / "test.log").open("wb") as log:
        subprocess.run(command, cwd=work, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=ROOT / "bin-int/Tests/GIMaskTests.exe")
    parser.add_argument("--baseline", action="store_true", help="记录仍将透明图缩到 512 的旧版")
    parser.add_argument("--lib", type=Path, default=ROOT / "bin/Debug-windows-x86_64/Redflag/Redflag.lib")
    parser.add_argument("--renderdoc", type=Path, help="RenderDoc DLL 路径；只捕获有铁丝时的帧")
    parser.add_argument("--distant", action="store_true", help="同一张原图全部绘进布片，测缩小时的覆盖率")
    args = parser.parse_args()
    exe = args.exe.resolve(strict=True)
    label = "before" if args.baseline else "after"
    OUT.mkdir(exist_ok=True, parents=True)
    if args.distant:
        label = "far-" + label
        files = run(exe, label, False, args.renderdoc)
        raster = gpu_image(files,"mask","raster")[:,:,2] > .9
        primary = gpu_image(files,"mask","ray")[:,:,2] > .9
        footprint = gpu_image(files,"opaque","raster")[:,:,2] > .9
        light = gpu_image(files,"mask","shadow")[:,:,0]
        shadow = light[footprint] == 0
        solid = np.array(Image.open(files/"mask.png"))[:,:,3] >= 128
        projected_width = 64 / (4*np.tan(np.radians(45)/2))
        assert int(footprint.sum()) == 1444 and np.mean(solid) == .25
        with (files/"picks.tsv").open() as source:
            picks = [p for p in csv.DictReader(source, delimiter="\t") if p["case"] in ("mask","all_hole")]
        assert all(int(p["entity"]) == int(raster[int(p["y"]),int(p["x"])]) for p in picks)
        result = dict(fixed_camera=[0,0,3], image_size=[1024,32], viewport=[64,64], u_texels=[0,1024],
                      projected_quad_width_px=projected_width, texels_horizontally_per_pixel=1024/projected_width,
                      area_pixels=int(footprint.sum()), original_opacity_coverage=float(solid.mean()),
                      raster_covered_pixels=int(raster.sum()), raster_coverage=float(raster[footprint].mean()),
                      primary_covered_pixels=int(primary.sum()), primary_coverage=float(primary[footprint].mean()),
                      shadowed_pixels=int(shadow.sum()), shadow_coverage=float(shadow.mean()),
                      pick_reads=len(picks), picks_on_wire=sum(int(p["entity"]) == 1 for p in picks),
                      exe_sha256=digest(exe), library_sha256=digest(args.lib),
                      device=(files/"device.txt").read_text().splitlines()[0])
        export(label + "-raster",raster)
        export(label + "-ray",primary)
        export(label + "-wall",light/.26)
        (OUT/(label+".json")).write_text(json.dumps(result,indent=2)+"\n",encoding="utf8")
        print(label, result)
        return
    clear = run(exe, label, True, args.renderdoc)
    references = {"other-opacity": clear / "open-fence.rfprobes"}
    if not args.baseline:
        references["previous-build"] = OUT / "before.rfprobes"
    files = run(exe, label, False, args.renderdoc, references)
    for name in ("mask.png","opacity.png"):
        (OUT / name).write_bytes((files / name).read_bytes())
    report = dict(device=(files / "device.txt").read_text().splitlines()[0], viewport=[64,64],
                  texture_size=[1024,32], opaque_columns="x mod 4 == 0", cutoff=.5, u_texels=[512.125,524.125],
                  test_exe_sha256=digest(exe), library_sha256=digest(args.lib), build="Debug x64 Vulkan", cases={})
    wall = gpu_image(files,"receiver","shadow")[:, :, 0]
    geometry = gpu_image(files,"opaque","raster")[:, :, 2] > .9
    with (files / "picks.tsv").open() as source:
        picks = list(csv.DictReader(source, delimiter="\t"))
    for case in ("mask","red_map","obj_original"):
        surface = gpu_image(files,case,"raster")[:, :, :4]
        rays = gpu_image(files,case,"ray")[:, :, :4]
        light = gpu_image(files,case,"shadow")[:, :, 0]
        wire = surface[:, :, 2] > .9
        assert np.array_equal(wire, gpu_image(files,"mask","raster")[:, :, 2] > .9), case
        assert (wire & ~geometry).sum() == 0
        columns = np.flatnonzero(wire[32]).tolist()
        assert len(columns) >= 6 and max(np.diff(columns)) >= 4
        assert (surface[geometry & ~wire,2] < .1).all()
        assert np.allclose(gpu_image(clear,case,"shadow")[:,:,0], wall)
        checked = [p for p in picks if p["case"] == case or (case=="mask" and p["case"]=="all_hole")]
        assert {int(p["entity"]) for p in checked} == {0,1}
        assert all(int(p["entity"]) == int(wire[int(p["y"]),int(p["x"])]) for p in checked)
        wrong_primary = np.any(np.abs(surface - rays) > .0001, axis=2)
        expected = np.where(wire, 0, wall)
        wrong_shadow = np.abs(light - expected) > 1e-5
        if args.baseline:
            assert np.array_equal(wrong_primary, wire) and np.array_equal(wrong_shadow, wire), case
        else:
            assert not wrong_primary.any() and not wrong_shadow.any(), (case,wrong_primary.sum(),wrong_shadow.sum())
        x, y = columns[len(columns)//2], 32
        report["cases"][case] = dict(visible_wire_pixels=int(wire.sum()), raster_wire_columns_at_y32=columns,
              pick_reads=len(checked), mismatched_pick_reads=0, nearest_surface_errors=int(wrong_primary.sum()),
              direct_shadow_errors=int(wrong_shadow.sum()), sample_xy=[x,y], raster_z=float(surface[y,x,2]),
              ray_z=float(rays[y,x,2]), wall_red=float(light[y,x]), wall_red_without_wires=float(wall[y,x]))
        if case == "mask":
            export(label + "-raster", wire)
            export(label + "-ray", rays[:,:,2] > .9)
            export(label + "-wall", light/.26)
    # 比较同一网格位置的 +Z 半球，不改变光线的随机序列。探针前方是铁丝，灯牌在铁丝另一侧。
    probe = {}
    grids = {}
    for name,source in (("wires",files),("clear",clear)):
        with (source / "original-probe.tsv").open() as point:
            row = next(csv.DictReader(point,delimiter="\t"))
        rgb = [float(row[k]) for k in ("red","green","blue")]
        assert np.allclose(np.array(rgb), np.array([4,.5,.125]) * round(rgb[0]*16) / 64)
        probe[name] = dict(cell=[int(row[k]) for k in ("cell_x","cell_y","cell_z")],
                          xyz=[float(row[k]) for k in ("world_x","world_y","world_z")],
                          rgb=rgb, emitter_hits_out_of_64=round(rgb[0]*16))
        gpu = (source / "original-probes.bin").read_bytes()
        assert gpu == (source / "open-fence.rfprobes").read_bytes()[24:]
        grid = np.frombuffer(gpu,"<f4").reshape(7,6,12,6,2,4)[:,:,:,:,0,:3]
        grids[name] = grid
        # 房间的 x 范围为 -1 到 1。x=-0.25 投到布片的第 516.625 列，正好落在一根实心铁丝上。
        wire_rgb = grid[2,3,4,4]
        probe[name]["under_wire"] = dict(cell=[4,3,2], xyz=[-.25,1/6,2.5/7], direction="+Z",
                                         rgb=wire_rgb.tolist(), emitter_hits_out_of_64=round(float(wire_rgb[0])*16))
    assert probe["wires"]["xyz"] == probe["clear"]["xyz"]
    a,b = (probe[k]["under_wire"]["emitter_hits_out_of_64"] for k in ("wires","clear"))
    assert (a == b if args.baseline else a < b), probe
    changed = np.any(grids["wires"] != grids["clear"], axis=4)
    assert (changed.sum() == 0 if args.baseline else changed.sum() > 0)
    assert np.all(grids["wires"] <= grids["clear"])
    report["changed_probe_directions"] = int(changed.sum())
    report["probe_plus_z"] = probe
    with (files/"probe-load.tsv").open() as f:
        loaded = {r["case"]:bool(int(r["loaded_original_bake"])) for r in csv.DictReader(f,delimiter="\t")}
    assert loaded["other-opacity"] == args.baseline and loaded["same"]
    if not args.baseline:
        assert not loaded["previous-build"]
    keys = {}
    for name,source in (("wires",files),("clear",clear)):
        data = (source/"open-fence.rfprobes").read_bytes()
        keys[name] = hex(struct.unpack_from("<Q",data,8)[0])
        if name == "wires": (OUT/(label+".rfprobes")).write_bytes(data)
    assert (keys["wires"] == keys["clear"]) == args.baseline
    report["bake_compatibility"] = dict(loaded=loaded, scene_keys=keys)
    (OUT/(label+".json")).write_text(json.dumps(report,indent=2)+"\n",encoding="utf8")
    print(label, json.dumps(report["cases"]["mask"]), json.dumps(probe))


if __name__ == "__main__":
    main()
