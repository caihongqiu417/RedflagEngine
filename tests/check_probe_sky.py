"""从天窗看均匀彩色天空，比较黑墙、白墙与封闭房间烘焙到探针的线性 RGB。"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

import numpy as np

from check_directional_bake import bake, inspect
from check_probe_baker import BIN, ENTRY, ROOT, dump_yaml, gui_bake, sha
from check_probe_save_as import save
from profile_native_navigation import prepare


def room(ceiling=False, gray=0):
    panels = [([0, -.05, 0], [4.2, .1, 4.2]), ([-2.05, 1, 0], [.1, 2, 4.2]),
              ([2.05, 1, 0], [.1, 2, 4.2]), ([0, 1, -2.05], [4, 2, .1]), ([0, 1, 2.05], [4, 2, .1])]
    if ceiling:
        panels.append(([0, 2.05, 0], [4.2, .1, 4.2]))
    entities = [dict(Entity=i+1, TagComponent=dict(Tag="墙"),
                     TransformComponent=dict(Translation=position, Rotation=[0,0,0], Scale=size),
                     MeshRendererComponent=dict(Color=[1,1,1,1], BaseColor=[gray,gray,gray,1], Metallic=0, Roughness=1))
                for i, (position, size) in enumerate(panels)]
    return dict(Scene="天窗", Environment="sky.hdr", Entities=entities)


def write_sky(path, rgbe):
    # Radiance RGBE 保存公共指数。下面的 4 × 2 个像素相同，得到的世界各方向亮度也相同。
    path.write_bytes(b"#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 4\n" + bytes(rgbe) * 8)


def write_archive(path):
    # RFIBL01 里的漫反射图是黑色，只有无粗糙度的六面天空有颜色。探针应与上面的 HDR 得到相同读数。
    rgb = np.tile(np.array([1,.5,.25], dtype="<f2"), 6 * 128 * 128)
    with path.open("wb") as stream:
        stream.write(b"RFIBL01\n")
        stream.seek(6 * (2048**2 + 32**2) * 3 * 2, 1)
        stream.write(rgb.tobytes())
        remaining = sum(6 * (128 >> level)**2 * 3 * 2 for level in range(1, 5)) + 512**2 * 2 * 2
        stream.truncate(stream.tell() + remaining)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=BIN/"RedflagForge.exe")
    parser.add_argument("--before", type=Path)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix="probe-sky-", dir=ROOT/"tmp"))
    prepare(work)
    exe = args.exe.resolve()
    env = dict(os.environ, PATH=str(BIN)+os.pathsep+os.environ["PATH"], RF_VULKAN_VALIDATION="1")
    path = work/ENTRY
    sky = path.parent/"sky.hdr"
    write_sky(sky, (128,64,32,129))  # 线性 RGB (1, 0.5, 0.25)。
    records = {}
    def sample(label, scene, feedback=False, program=exe):
        settings = dict(Shading="DDGI", DDGIProbeFeedback=feedback, PreviewWidth=160)
        data, gpu = bake(program, work, env, label, dump_yaml(scene), dump_yaml(settings))
        rgb, records[label] = inspect(data)
        records[label]["gpu"] = gpu
        return data, rgb

    if args.before:
        _, old = sample("before", room(), program=args.before.resolve())
        assert not old.any()
    original, opened = sample("open", room())
    _, closed = sample("closed", room(ceiling=True))
    large = room(ceiling=True)
    for panel in large["Entities"]:
        for field in ("Translation", "Scale"):
            panel["TransformComponent"][field] = [x * 3000 for x in panel["TransformComponent"][field]]
    _, far_roof = sample("large-closed", large)
    archive = sky.with_suffix(".rfibl")
    write_archive(archive)
    _, archived = sample("archive", dict(room(), Environment=archive.name))
    archive.unlink()
    np.testing.assert_array_equal(archived, opened)
    _, single = sample("white-one-round", room(gray=.8))
    _, multiple = sample("white-four-rounds", room(gray=.8), feedback=True)
    # 有限的天窗占据朝上半球的一部分。开口附近很亮，仍达不到无遮挡天空的全部亮度。
    assert .9 < opened[:,0].max() <= 1.00002
    np.testing.assert_allclose(opened[:,1:], opened[:,0:1] * np.array([.5,.25]), atol=2e-5)
    assert not closed.any() and not far_roof.any()
    # 黑墙不反光。朝上的探针只能收到穿过 4 × 4 天窗的光。
    # 在窗面上积分 h² / (π r⁴)，h 是探针到窗面的高度，r 是探针到该像素的距离。
    side = 256
    coord = (np.arange(side) + .5) * 4 / side - 2
    x, z = np.meshgrid(coord, coord)
    aperture = []
    for ix in (0, 5):
        for iy in (0, 2, 5):
            px = -2.1 + (ix + .5) * 4.2 / 12
            py = -.1 + (iy + .5) * 2.1 / 6
            h = 2 - py
            exact = np.sum(h*h / ((x-px)**2 + z*z + h*h)**2) * (4 / side)**2 / np.pi
            face = ((3 * 6 + iy) * 12 + ix) * 6 + 2
            measured = float(opened[face,0])
            aperture.append(dict(x=px, y=py, z=0, reference=exact, measured=measured))
    records["window"] = aperture
    assert max(abs(p["reference"]-p["measured"]) for p in aperture) < .04
    np.testing.assert_allclose(single, opened, atol=2e-5)
    # 朝地面的探针第一轮只看见地板。后续轮次里，白地板把收到的天光送回房间。
    below = np.arange(len(single)) % 6 == 3
    assert single[below].max() == 0 and multiple[below,0].mean() > .01
    records["downward_red_mean"] = [float(rgb[below,0].mean()) for rgb in (single,multiple)]

    # 路径和时间戳保持原样，只换天空像素。另存为必须核对重新读取的内容。
    stamp = sky.stat()
    write_sky(sky, (32,64,128,129))
    os.utime(sky, ns=(stamp.st_atime_ns,stamp.st_mtime_ns))
    changed, blue = sample("same-path-blue", room())
    np.testing.assert_allclose(blue, opened[:, ::-1], atol=2e-5)
    assert original[8:16] != changed[8:16]
    probes = Path(str(path)+".rfprobes")
    write_sky(sky, (128,64,32,129))
    os.utime(sky, ns=(stamp.st_atime_ns,stamp.st_mtime_ns))
    def swap_sky():
        write_sky(sky, (32,64,128,129))
        os.utime(sky, ns=(stamp.st_atime_ns,stamp.st_mtime_ns))
        probes.unlink()
    assert gui_bake(exe, work, env, probes, before_shortcut=swap_sky) == 0
    assert probes.read_bytes() == changed
    records["editor_rebake_matches_cli"] = sha(changed)
    probes.write_bytes(original)
    dest = work/"new-sky.redflag"
    save(exe, work, env, dest)
    assert dest.is_file() and not Path(str(dest)+".rfprobes").exists()
    records["editor_rejected_old_sky"] = True

    # 烘焙开始前拿走天空文件，正式程序必须保留上次成功烘焙的光。
    assert gui_bake(exe, work, env, probes, before_shortcut=sky.unlink, error_message=b"Probe bake failed") == 0
    assert probes.read_bytes() == original
    failed = subprocess.run([str(exe), "--bake-probes", str(ENTRY)], cwd=work, env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
    assert failed.returncode and probes.read_bytes() == original
    (work/"missing.log").write_bytes(failed.stdout)
    records["missing_environment_kept_file"] = sha(original)
    if args.report:
        args.report.parent.mkdir(parents=True,exist_ok=True)
        args.report.write_text(json.dumps(records,indent=2)+"\n",encoding="utf8")
    print(work, records["downward_red_mean"])


if __name__ == "__main__":
    main()
