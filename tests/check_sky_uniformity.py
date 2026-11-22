"""用无遮挡的平面检查天光烘焙。每个朝上的探针都应收到相同的光。"""
import argparse
import json
import os
from pathlib import Path
import shutil
import tempfile

import numpy as np

from check_directional_bake import bake, inspect
from check_probe_baker import BIN, ENTRY, FORGE, ROOT, dump_yaml
from check_probe_sky import write_archive
from profile_native_navigation import prepare


def softbox(path):
    # 128² 立方体上面放一块很亮的方形灯。其余方向保留相同的环境底光。
    write_archive(path)
    base = np.array([.125, .0625, .03125])
    lamp = np.array([64., 32., 16.])
    faces = np.empty((6, 128, 128, 3), dtype="<f2")
    faces[:] = base
    faces[2, 71:79, 80:88] = lamp
    with path.open("r+b") as stream:
        stream.seek(8 + 6 * (2048**2 + 32**2) * 3 * 2)
        stream.write(faces.tobytes())

    # 立方体上面的方向写作 (u,1,v)。立体角乘余弦后得到 du dv / (1+u²+v²)²，再除以 π。
    # 灯外天空恒定，只在灯片和双线性采样的半像素边缘上做数值积分。
    scale = 32
    x = 79 + (np.arange(10 * scale) + .5) / scale
    y = 70 + (np.arange(10 * scale) + .5) / scale
    u, v = np.meshgrid(2 * x / 128 - 1, 2 * y / 128 - 1)
    coverage = np.outer(np.clip(np.minimum(y - 70.5, 79.5 - y), 0, 1),
                        np.clip(np.minimum(x - 79.5, 88.5 - x), 0, 1))
    weight = (coverage / (1 + u*u + v*v)**2).sum() * (2 / (128 * scale))**2 / np.pi
    return base + (lamp - base) * weight


def studio_reference():
    import cv2
    path = FORGE / "assets/textures/studio_small_09_1k.hdr"
    image = cv2.imread(str(path), cv2.IMREAD_UNCHANGED)
    if image is None:
        raise FileNotFoundError("Run python tests/fetch_camera_studio.py to download the HDR panorama.")
    h = image.shape[0] // 2
    edges = np.linspace(0, np.pi / 2, h + 1)
    # 对原始经纬图的每条纬带积分。GPU 读取的立方体分辨率较低，这里保留原图作为独立参考。
    return np.diff(np.sin(edges)**2) @ image[:h, :, ::-1].mean(axis=1, dtype=np.float64)


def main(exe, label):
    work = Path(tempfile.mkdtemp(prefix="sky-flat-", dir=ROOT / "tmp"))
    prepare(work)
    env = dict(os.environ, PATH=str(BIN) + os.pathsep + os.environ["PATH"], RF_VULKAN_VALIDATION="1")
    entry = work / ENTRY
    # 导入器会在模型旁保存 .rfmeta，让这份只用于测量的副本留在临时项目里。
    mesh = work / "plane.gltf"
    shutil.copyfile(ROOT / "tests/fixtures/TextureQuad.gltf", mesh)
    material = dict(ModelID=0, FilePath=mesh.name, Color=[1,1,1,1])
    plane = dict(Entity=1, TagComponent=dict(Tag="平面"), ModelRendererComponent=material,
                 TransformComponent=dict(Translation=[0,0,0], Rotation=[-np.pi/2,0,0], Scale=[1,1,1]))
    refs = {"softbox.rfibl": softbox(entry.parent / "softbox.rfibl"),
            "../textures/studio_small_09_1k.hdr": studio_reference()}
    out = {}
    for path, reference in refs.items():
        config = dump_yaml(dict(Scene="平面", Environment=path, Entities=[plane]))
        settings = dump_yaml(dict(Shading="DDGI", DDGIProbeFeedback=False, PreviewWidth=160))
        data, device = bake(exe, work, env, Path(path).stem, config, settings)
        colors, _ = inspect(data)
        up = colors.reshape(-1, 6, 3)[:, 2].astype(np.float64)
        error = up - reference
        out[Path(path).stem] = dict(driver=device, probes=len(up), reference_rgb=reference.tolist(),
                                   average_rgb=up.mean(axis=0).tolist(), red_min=float(up[:,0].min()),
                                   red_max=float(up[:,0].max()), rgb_rmse=float(np.sqrt(np.mean(error**2))),
                                   red_standard_deviation=float(np.std(up[:,0])))
    output = ROOT / "output/validation/sky-probes"
    output.mkdir(parents=True, exist_ok=True)
    report = output / f"flat-{label}.json"
    report.write_text(json.dumps(out, indent=2) + "\n", encoding="utf8")
    print(report)
    return out


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("label", choices=["first", "filtered"])
    parser.add_argument("exe", type=Path)
    args = parser.parse_args()
    current = main(args.exe.resolve(strict=True), args.label)
    if args.label == "filtered":
        first = json.loads((ROOT / "output/validation/sky-probes/flat-first.json").read_bytes())
        for scene, values in current.items():
            assert values["rgb_rmse"] < first[scene]["rgb_rmse"]
            # 平面朝上的 504 个方向都能看见整片天空，世界位置不应改变收到的光。
            assert values["red_standard_deviation"] < 1e-6
        # 人工灯箱直接写入 128² 的天空面。摄影棚则从原始全景缩到 128²，再与原图比较。
        assert current["softbox"]["rgb_rmse"] < .00005
        assert current["studio_small_09_1k"]["rgb_rmse"] < .002
