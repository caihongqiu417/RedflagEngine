"""从已标定的照片重建 3D 高斯，并把 NPC 使用的机位烘焙为一组可以单独读取的图片。"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import shutil
import subprocess
import tempfile


def region_directions(view, calibration, frame):
    if not view.get("regions"):
        return
    camera = {**calibration, **frame}
    if camera.get("camera_model", "PINHOLE") not in ("PINHOLE", "OPENCV") or any(
            camera.get(key, 0) for key in ("k1", "k2", "k3", "k4", "p1", "p2")):
        raise ValueError("请先校正镜头畸变，再在校正后的照片上标注区域")
    width, height = camera["w"], camera["h"]
    fx, fy = camera["fl_x"], camera["fl_y"]
    if width <= 0 or height <= 0 or fx <= 0 or fy <= 0:
        raise ValueError("区域标注需要正的图像尺寸和焦距")
    view["size"] = [width, height]
    for region in view["regions"].values():
        left, top, right, bottom = region["bounds"]
        if not (0 <= left < right <= width and 0 <= top < bottom <= height):
            raise ValueError(f"{view['camera']} 的区域 {region['label']} 超出参考图")
        # 图中向下为正，相机向上为正。矩阵前三列依次给出相机的右、上、后方向。
        x = ((left + right) / 2 - camera.get("cx", width / 2)) / fx
        y = (camera.get("cy", height / 2) - (top + bottom) / 2) / fy
        direction = [row[0] * x + row[1] * y - row[2] for row in frame["transform_matrix"][:3]]
        length = math.hypot(*direction)
        region["direction"] = [v / length for v in direction]


def bake(brush, dataset, annotations, output, steps, degree):
    project = json.loads(annotations.read_bytes())
    calibration = json.loads((dataset / "transforms_val.json").read_bytes())
    frames = calibration["frames"]
    cameras = {Path(frame["file_path"]).stem: frame for frame in frames}
    if len(cameras) != len(frames):
        raise ValueError("候选机位的照片须使用不同的文件名")
    if any(view["camera"] not in cameras for view in project["views"]):
        raise ValueError("候选机位必须出现在 transforms_val.json 中")
    for view in project["views"]:
        region_directions(view, calibration, cameras[view["camera"]])

    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=output) as name:
        work = Path(name)
        subprocess.run([str(brush.resolve()), str(dataset.resolve()), "--total-steps", str(steps),
                        "--eval-every", str(steps), "--export-every", str(steps), "--eval-save-to-disk",
                        "--export-path", str(work.resolve()), "--export-name", "scene.ply",
                        "--sh-degree", str(degree)], check=True)
        # 相机也可能单独调整。把模型和本次选中的图片一起编号，旧索引就始终指向旧照片。
        model = work / "scene.ply"
        with model.open("rb") as stream:
            checksum = hashlib.file_digest(stream, "sha256")
        for camera in sorted({view["camera"] for view in project["views"]}):
            filename = f"{camera}.png"
            checksum.update(filename.encode("utf-8") + b"\0")
            with (work / f"eval_{steps}" / filename).open("rb") as stream:
                checksum.update(hashlib.file_digest(stream, "sha256").digest())
        digest = checksum.hexdigest()
        images = output / digest
        images.mkdir(exist_ok=True)
        model.replace(images / "scene.ply")
        for view in project["views"]:
            key = view["camera"]
            image = work / f"eval_{steps}" / f"{key}.png"
            shutil.copyfile(image, images / image.name)
            view["image"] = f"{digest}/{image.name}"
            view["pose"] = cameras[key]["transform_matrix"]
        index = work / "index.json"
        index.write_text(json.dumps(project, ensure_ascii=False, indent=2), encoding="utf-8")
        index.replace(output / "index.json")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for arg in ("brush", "dataset", "annotations", "output"):
        parser.add_argument(arg, type=Path)
    parser.add_argument("--steps", type=int, default=30000)
    parser.add_argument("--sh-degree", type=int, choices=range(4), default=3)
    args = parser.parse_args()
    bake(args.brush, args.dataset, args.annotations, args.output, args.steps, args.sh_degree)
