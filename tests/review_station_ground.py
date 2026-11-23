"""以同一机位比较两个发行包。逐帧将角色网格的最低点与脚下地面三角形相交求高度差。"""

import argparse
import base64
import json
import math
import shutil
import subprocess
from pathlib import Path
from zipfile import ZipFile

import numpy as np
import yaml
from PIL import Image, ImageDraw, ImageFont

from capture_station_ground import SCENE

CASES = ("side", "jump", "watt", "original")
VIEWS = [
    ("蹲伏", "side", 1.8),
    ("地面倒地", "side", 6.8),
    ("跳起后倒地", "jump", 6.8),
    ("瓦特倒地", "watt", 6.8),
]


def ground_meshes(archive):
    result = []
    with ZipFile(archive) as package:
        scene = yaml.safe_load(package.read(SCENE))
        for entity in scene["Entities"]:
            path = Path(entity.get("ModelRendererComponent", {}).get("FilePath", ""))
            if path.name not in {"Road.gltf", "Gravel.gltf", "Tracks.gltf"}:
                continue
            doc = json.loads(package.read(path.as_posix()))

            def read(index):
                a = doc["accessors"][index]
                view = doc["bufferViews"][a["bufferView"]]
                uri = doc["buffers"][view["buffer"]]["uri"]
                buffer = (
                    base64.b64decode(uri.split(",", 1)[1])
                    if uri.startswith("data:")
                    else package.read((path.parent / uri).as_posix())
                )
                dtype = np.dtype({5126: "<f4", 5123: "<u2", 5125: "<u4"}[a["componentType"]])
                cols = {"SCALAR": 1, "VEC3": 3}[a["type"]]
                return np.ndarray(
                    (a["count"], cols),
                    dtype=dtype,
                    buffer=buffer,
                    offset=view.get("byteOffset", 0) + a.get("byteOffset", 0),
                    strides=(view.get("byteStride", cols * dtype.itemsize), dtype.itemsize),
                )

            transform = entity["TransformComponent"]
            x, y, z = transform["Rotation"]
            s, c = math.sin, math.cos
            rx = [[1, 0, 0], [0, c(x), -s(x)], [0, s(x), c(x)]]
            ry = [[c(y), 0, s(y)], [0, 1, 0], [-s(y), 0, c(y)]]
            rz = [[c(z), -s(z), 0], [s(z), c(z), 0], [0, 0, 1]]
            rotation = np.asarray(rz) @ ry @ rx
            for node in doc["nodes"]:
                if "mesh" not in node:
                    continue
                assert "rotation" not in node and "matrix" not in node
                for part in doc["meshes"][node["mesh"]]["primitives"]:
                    xyz = read(part["attributes"]["POSITION"]) * np.array(
                        node.get("scale", [1, 1, 1])
                    ) + node.get("translation", [0, 0, 0])
                    world = (xyz * transform["Scale"]) @ rotation.T + transform["Translation"]
                    result.extend(world[read(part["indices"]).reshape(-1, 3)])
    return np.stack(result).transpose(1, 0, 2)


def ground_y(mesh, x, z):
    # 在 XZ 平面求点落入了哪几个三角形，再插值得到可见的最高表面。
    a, b, c = mesh
    b, c = b - a, c - a
    dx, dz = x - a[:, 0], z - a[:, 2]
    det = b[:, 0] * c[:, 2] - b[:, 2] * c[:, 0]
    with np.errstate(invalid="ignore", divide="ignore"):
        u = (dx * c[:, 2] - dz * c[:, 0]) / det
        v = (b[:, 0] * dz - b[:, 2] * dx) / det
        heights = a[:, 1] + u * b[:, 1] + v * c[:, 1]
    inside = (abs(det) > 1.0e-9) & (u >= 0) & (v >= 0) & (u + v <= 1)
    return float(heights[inside].max())


def nearest(rows, t):
    return min(rows, key=lambda r: abs(r["time"] - t))


def load(directory, archive, output, verify):
    mesh = ground_meshes(archive)
    runs = {}
    report = {}
    for name in CASES:
        folder = directory / name
        take = json.loads((folder / "poses.json").read_bytes())
        rows = take["frames"]
        for row in rows:
            row["clearance_cm"] = {
                group: round((y - ground_y(mesh, x, z)) * 100, 4)
                for group, (x, y, z) in row["lowest"].items()
            }
        if name == "watt" or name == "jump" or name == "side":
            assert any(row["health"] == 100 for row in rows)
            assert any(
                row["clip"] == "Station-Down" and row["health"] == 0 and row["clip_time"] >= 0.99
                for row in rows
            )
        if name == "jump":
            in_air = next(row for row in rows if row["health"] == 0)
            assert in_air["root"][1] - ground_y(mesh, *in_air["root"][::2]) > 0.35, (
                "受击时必须实际在空中"
            )
        if name in ("original", "side"):
            assert any(row["clip"] == "Station-CrouchWalk" for row in rows)
            assert nearest(rows, 1.8)["clip"] == "Station-Crouch"
        if verify:
            # 检查贴地姿势保持期间每一个实际帧。鞋底几何和轻微呼吸可有 1 厘米余量。
            crouch = [r for r in rows if 1.35 <= r["time"] <= 2.06 and r["clip"] == "Station-Crouch"]
            settled = [r for r in rows if r["clip"] == "Station-Down" and r["clip_time"] >= 0.99]
            for r in crouch + settled:
                assert abs(r["clearance_cm"]["model"]) <= 1.0, (name, r["seq"], r["clearance_cm"])
        out = output / name
        out.mkdir(parents=True, exist_ok=True)
        # 每个采样帧独占一行，按帧号查找时能同时读到动作、脚底和躯干的位置。
        with (out / "poses.json").open("w", encoding="utf8") as stream:
            header = json.dumps({k: v for k, v in take.items() if k != "frames"}, ensure_ascii=False)
            stream.write(header[:-1] + ',\n"frames":[\n')
            stream.write(",\n".join(json.dumps(row, separators=(",", ":")) for row in rows))
            stream.write("\n]}\n")
        shutil.copy2(folder / "bones.jsonl.gz", out / "bones.jsonl.gz")
        runs[name] = (folder, take)
        times = {"站立": 0.55}
        if name != "watt":
            times.update(蹲伏=1.8, 蹲走=2.55)
        if name != "original":
            times.update(倒地定格=6.8)
        report[name] = {label: nearest(rows, t) for label, t in times.items()}
    return runs, report


def photograph(runs, case, t):
    folder, take = runs[case]
    frame = nearest(take["photos"], t)
    with Image.open(folder / "images" / f"{frame['seq']:04}.jpg") as image:
        return image.convert("RGB")


def visuals(before, after, out):
    normal = ImageFont.truetype("C:/Windows/Fonts/msyh.ttc", 22)
    small = ImageFont.truetype("C:/Windows/Fonts/msyh.ttc", 16)
    sheet = Image.new("RGB", (1050, 4 * 354 + 50), "#eef0f0")
    pen = ImageDraw.Draw(sheet)
    pen.text((12, 8), "更新前", font=normal, fill="#34454a")
    pen.text((538, 8), "更新后", font=normal, fill="#285648")
    for i, (title, case, seconds) in enumerate(VIEWS):
        for j, runs in enumerate((before, after)):
            frame = photograph(runs, case, seconds)
            # 两列截取相同的窗口区域，保留角色影子与脚下砂石的位置。
            crop = frame.crop((180, 48, 1180, 681)).resize((510, 323), Image.Resampling.LANCZOS)
            x, y = 10 + 525 * j, 48 + 354 * i
            sheet.paste(crop, (x, y))
            row = nearest(runs[case][1]["frames"], seconds)
            clearance = row["clearance_cm"]["model"]
            if abs(clearance) < 0.005:
                clearance = 0.0
            pen.text(
                (x, y + 324),
                f"{title}  ·  网格最低处距地面 {clearance:.2f} cm",
                font=small,
                fill="#283938",
            )
    sheet.save(out / "compare.jpg", quality=94)
    # 单栏书页用一对图。原机位保留整幅游戏窗口，方便对照正常游玩时看到的高度。
    airborne = Image.new("RGB", (1050, 402), "#eef0f0")
    airborne.paste(sheet.crop((0, 0, 1050, 48)), (0, 0))
    airborne.paste(sheet.crop((0, 48 + 2 * 354, 1050, 48 + 3 * 354)), (0, 48))
    airborne.save(out / "airborne.jpg", quality=94)
    original = Image.new("RGB", (1280, 402), "#eef0f0")
    for j, runs in enumerate((before, after)):
        frame = photograph(runs, "original", 1.8)
        original.paste(frame.resize((640, 360), Image.Resampling.LANCZOS), (640 * j, 42))
        ImageDraw.Draw(original).text(
            (12 + 640 * j, 8),
            "原关卡蹲伏 · " + ("更新前" if j == 0 else "更新后"),
            font=normal,
            fill="#283938",
        )
    original.save(out / "original.jpg", quality=94)
    # 按真实截帧间隔回放。两次运行从同一输入阶段起步，GPU 帧率会有差别。
    proc = subprocess.Popen(
        [
            "ffmpeg",
            "-hide_banner",
            "-loglevel",
            "error",
            "-y",
            "-f",
            "rawvideo",
            "-pixel_format",
            "rgb24",
            "-video_size",
            "1280x402",
            "-framerate",
            "24",
            "-i",
            "-",
            "-an",
            "-c:v",
            "libx264",
            "-pix_fmt",
            "yuv420p",
            "-crf",
            "17",
            "-movflags",
            "+faststart",
            str(out / "compare.mp4"),
        ],
        stdin=subprocess.PIPE,
    )
    for case, start, duration, title in [
        ("side", 0.40, 3.85, "站立、蹲伏、蹲走、起身"),
        ("side", 4.35, 2.85, "在地面受到放电"),
        ("jump", 4.10, 3.1, "跳起时受到放电"),
        ("watt", 4.40, 2.8, "瓦特倒地"),
    ]:
        for index in range(round(duration * 24)):
            view = Image.new("RGB", (1280, 402), "#213136")
            draw = ImageDraw.Draw(view)
            for j, runs in enumerate((before, after)):
                frame = photograph(runs, case, start + index / 24).resize(
                    (640, 360), Image.Resampling.LANCZOS
                )
                view.paste(frame, (640 * j, 42))
                draw.text(
                    (640 * j + 10, 8),
                    ("更新前" if j == 0 else "更新后") + "   " + title,
                    font=normal,
                    fill="white",
                )
            proc.stdin.write(view.tobytes())
    proc.stdin.close()
    assert proc.wait() == 0


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("before", "old_zip", "after", "new_zip", "output"):
        p.add_argument(name, type=Path)
    a = p.parse_args()
    before, old = load(a.before, a.old_zip, a.output / "before", False)
    after, new = load(a.after, a.new_zip, a.output / "after", True)
    (a.output / "summary.json").write_text(
        json.dumps({"before": old, "after": new}, ensure_ascii=False, indent=2) + "\n", encoding="utf8"
    )
    visuals(before, after, a.output)
