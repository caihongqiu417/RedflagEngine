"""在项目根目录运行 tests/run_gi_mask_tests.cmd 后，核对相机、阴影、离屏反射的整张 GPU 读回图。"""
import csv
import hashlib
import json
from pathlib import Path

import numpy as np
from PIL import Image

work = Path("bin-int/Tests/gi-mask")
output = Path("output/validation/gi-mask")
output.mkdir(exist_ok=True, parents=True)
W = 64


def image(case, stage):
    fields = 12 if stage in ("ray", "raster", "mirror-primary") else 4
    return np.fromfile(work / f"{case}-{stage}.bin", dtype="<f4").reshape(W, W, fields)


def picture(name, data):
    # 裁剪所有直接光图片时统一以 0.26 对应白色。保留原始 64×64 的像素。
    Image.fromarray(np.uint8(255 * np.clip(data, 0, 1) + .5)).save(output / (name + ".png"))


rs = image("mask", "raster")[:, :, 2]
solid = image("opaque", "raster")[:, :, 2] > .9
hole = solid & (rs < .1)
ring = solid & ~hole
open_wall = image("receiver", "shadow")[:, :, 0]
assert solid.sum() == 1444 and hole.sum() == 196
assert np.all(open_wall[solid] > 0)
checks = {}
samples = {}
with (work / "picks.tsv").open(encoding="utf8") as stream:
    picks = list(csv.DictReader(stream, delimiter="\t"))
all_hole = [p for p in picks if p["case"] == "all_hole"]
assert len({(int(p["x"]), int(p["y"])) for p in all_hole}) == 196
assert all(hole[int(p["y"]), int(p["x"])] and int(p["entity"]) == 0 for p in all_hole)
for name in dict.fromkeys(p["case"] for p in picks if p["case"] != "all_hole"):
    raster = image(name, "raster")[:, :, 2]
    rays = image(name, "ray")[:, :, 2]
    shadow = image(name, "shadow")[:, :, 0]
    assert np.isfinite(rays).all() and np.isfinite(shadow).all(), name
    front = raster > .9
    backstop = (raster > .4) & (raster < .6)
    # 挡板的距离与镜头一致，分别投射观察光线和阴影光线。后面实心小片也是最近可见表面。
    surfaces = image(name, "raster")[:, :, :4]
    nearest = image(name, "ray")[:, :, :4]
    mismatch = np.any(np.abs(surfaces - nearest) > .0001, axis=2)
    expected = np.where(front | backstop, 0, open_wall)
    shadow_diff = np.abs(shadow - expected)
    checks[name] = dict(visible_sheet_pixels=int(front.sum()), visible_backstop_pixels=int(backstop.sum()),
                        ray_raster_mismatch=int(mismatch.sum()), shadow_raster_mismatch=int((shadow_diff > 1e-5).sum()),
                        lit_hole_pixels=int((shadow[hole] > 0).sum()),
                        hole_mean_linear_red=float(shadow[hole].mean()))
    case_picks = [p for p in picks if p["case"] == name]
    samples[name] = []
    for point in case_picks:
        x, y = int(point["x"]), int(point["y"])
        expected_id = 1 if front[y, x] else 2 if backstop[y, x] else 0
        assert int(point["entity"]) == expected_id, (name, x, y, point, expected_id)
        samples[name].append(dict(x=x, y=y, picked_id=expected_id, raster_z=float(raster[y,x]),
                                  ray_z=float(rays[y,x]), receiver_linear_red=float(shadow[y,x])))
    if name in ("receiver", "mask", "opaque", "backstop"):
        picture(name + "-direct", shadow / .26)
    if name == "mask":
        picture("raster-visible-sheet", front)

# 三角形完全相同，唯一变化是图片、材质或透明度。检查中央、布边与覆盖范围。
for name in ("mask", "red_map", "restore_base", "obj_original"):
    assert checks[name]["visible_sheet_pixels"] == int(ring.sum()), (name, checks[name])
for name in ("opaque", "equal", "remove_base", "obj_remove"):
    assert checks[name]["visible_sheet_pixels"] == int(solid.sum()), (name, checks[name])
for name in ("below", "entity_below", "red_product"):
    assert checks[name]["visible_sheet_pixels"] == 0, (name, checks[name])
assert samples["entity_equal"][1]["picked_id"] == 1  # 完全实心处：0.8 × 0.625 恰好等于 0.5。
assert int(hole.sum()) == checks["replace_base"]["visible_sheet_pixels"]
assert samples["red_linear"][1]["picked_id"] == 1  # R=128/255，大于 0.4；误按 sRGB 解码会掉到 0.216。
assert 0 < checks["backstop"]["visible_backstop_pixels"] < 196
assert checks["mask"]["lit_hole_pixels"] == 196
assert np.array_equal(image("mask", "shadow")[:, :, :3][hole], image("receiver", "shadow")[:, :, :3][hole])
# 把测试里真正发现的像素分歧逐项报出，便于区分轮廓偏移与整块布错误遮光。
assert all(c["ray_raster_mismatch"] == 0 for c in checks.values()), checks
assert all(c["shadow_raster_mismatch"] == 0 for c in checks.values()), checks

reflection = {}
for name in ("receiver", "mask", "opaque", "backstop"):
    seen = image(name, "mirror-primary")
    assert np.allclose(seen[:, :, 2], -.5, atol=.0001) and (seen[:, :, 6] > .99).all()
    rgb = image(name, "mirror")[:, :, :3]
    reference = image("receiver", "mirror")[:, :, :3]
    assert np.allclose(reference[32,32], [4,.5,.125], atol=.001)
    lit = rgb[:, :, 0] > 3.99
    count = int(lit.sum())
    assert np.allclose(rgb[lit], reference[lit], atol=.001)
    reflection[name] = dict(reflected_lit_pixels=count, center_rgb=rgb[32,32].tolist(),
                            inside_hole_rgb=rgb[32,37].tolist(), on_sheet_rgb=rgb[32,45].tolist())
    picture("mirror-" + name, rgb / 4)
assert reflection["mask"]["center_rgb"] == reflection["receiver"]["center_rgb"]
assert reflection["opaque"]["center_rgb"] == [0, 0, 0]
assert reflection["backstop"]["center_rgb"] == [0, 0, 0]
assert reflection["opaque"]["reflected_lit_pixels"] < reflection["backstop"]["reflected_lit_pixels"] < reflection["mask"]["reflected_lit_pixels"] < reflection["receiver"]["reflected_lit_pixels"]

with (work / "probe-load.tsv").open() as stream:
    loads = {row["case"]: bool(int(row["loaded_original_bake"])) for row in csv.DictReader(stream, delimiter="\t")}
assert loads == {"original": True, "same": True, "cutoff": False, "replace_opacity": False,
                 "remove_opacity": False, "restore": True}, loads
probes = {}
for case, bake in (("original", "open"), ("remove_opacity", "sealed")):
    with (work / (case + "-probe.tsv")).open() as stream:
        row = next(csv.DictReader(stream, delimiter="\t"))
    # rfprobes 的 24 字节文件头之后是和 GPU 相同顺序的 RGB 和距离记录。
    gpu = (work / (case + "-probes.bin")).read_bytes()
    filename = bake + "-fence.rfprobes"
    saved = (work / filename).read_bytes()
    assert gpu == saved[24:]
    (output / filename).write_bytes(saved)
    probes[bake] = dict(cell=[int(row[k]) for k in ("cell_x","cell_y","cell_z")], direction="+Z",
                        world=[float(row[k]) for k in ("world_x","world_y","world_z")],
                        rgb=[float(row[k]) for k in ("red","green","blue")],
                        all_probe_bytes_match_saved_file=True, sha256=hashlib.sha256(gpu).hexdigest())
assert probes["open"]["world"] == probes["sealed"]["world"]
assert probes["open"]["rgb"][0] > 0 and probes["sealed"]["rgb"] == [0, 0, 0], probes
for case in probes.values():
    hits = round(64 * case["rgb"][0] / 4)
    assert np.allclose(case["rgb"], np.array([4., .5, .125]) * hits / 64, atol=1e-5)
    case["rays_hitting_emitter_from_64"] = hits

device, api, driver = (work / "device.txt").read_text().splitlines()
api = int(api)
result = dict(device=device, vulkan=f"{api >> 22}.{(api >> 12) & 1023}.{api & 4095}", raw_driver=int(driver),
              build="Debug x64", viewport=[W, W], linear_red_unit="linear HDR red channel before display mapping",
              camera_point_light_position=[0,0,3], receiver_z=0, sheet_z=1, backstop_z=.5,
              light_rgb=[10,10,10], light_radius=8, camera_vertical_fov_degrees=45,
              image_mapping="direct PNG: red/0.26; mirror PNG: RGB/4; fixed linear mapping",
              solid_pixels=int(solid.sum()), ring_pixels=int(ring.sum()), hole_pixels=int(hole.sum()),
              picked_receiver_in_hole=len(all_hole), cases=checks, samples=samples, offscreen_mirror=reflection,
              probe_loads=loads, probes=probes,
              debug_library_sha256=hashlib.sha256(Path("bin/Debug-windows-x86_64/Redflag/Redflag.lib").read_bytes()).hexdigest())
(output / "after.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf8")
print(f"{len(checks)} camera and shadow cases; {len(reflection)} mirror cases. See {output}/after.json")
