"""将指定版本的正式 RenderDoc 回放、像素差与两张原生面板图归档。"""
import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw
import yaml


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "output/profiling/triangle-intersection"


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def write_json(name, data):
    (OUT / name).write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf8")


def read_colors(work, name):
    data = np.fromfile(work / (name + ".f32"), dtype="<f4")
    return data.reshape(-1, 8 if name == "rays-probe" else 4)[:, :3]


def display_bytes(rgb):
    return np.rint(np.clip(rgb, 0, 1) * 255).astype(np.uint8)


def draw_boxes(centers, bounds, camera, low, high, name):
    scale = 710 / (high - low).max()
    def to_pixel(point):
        return (point - [low[0], high[1]]) * [scale, -scale] + 12
    width, height = np.ceil((high - low) * scale).astype(int) + 25
    x, y = np.rint(to_pixel(centers)).astype(int).T
    inside = (x >= 0) & (x < width) & (y >= 0) & (y < height)
    x, y = x[inside], y[inside]
    density = np.bincount(y * width + x, minlength=width * height).reshape(height, width)
    shade = 242 - 170 * np.clip(np.log1p(density) / np.log(35), 0, 1)
    img = Image.fromarray(shade.astype(np.uint8)).convert("RGB")
    draw = ImageDraw.Draw(img, "RGBA")
    for box, color in zip(bounds, [(24, 111, 110), (164, 77, 43)]):
        start, end = to_pixel(box)
        rect = (start[0], end[1], end[0], start[1])
        draw.rectangle(rect, fill=(*color, 20), outline=(*color, 255), width=4)
    x, y = to_pixel(camera)
    draw.ellipse((x - 6, y - 6, x + 6, y + 6), fill=(23, 37, 47), outline="white", width=2)
    img.save(OUT / name)


def tree_plan(case, children, name):
    # 深浅表示落在每格里的三角形重心数。两版都从相同区域重新绘制长廊放大图。
    triangles = np.fromfile(case / "rays-triangles.f32", dtype="<f4").reshape(-1, 4, 4)
    centers = triangles[:, :3, :3].mean(axis=1)[:, [0, 2]]
    bounds = np.array([[child["low"], child["high"]] for child in children])[:, :, [0, 2]]
    scene_file = case / "assets/scenes/Sponza-Atrium.redflag"
    entities = yaml.safe_load(scene_file.read_text(encoding="utf8"))["Entities"]
    camera = next(entity for entity in entities if entity.get("CameraComponent", {}).get("Primary"))
    x, _, z = camera["TransformComponent"]["Translation"]
    position = np.array([x, z])
    draw_boxes(centers, bounds, position, bounds[:, 0].min(axis=0), bounds[:, 1].max(axis=0), name)
    draw_boxes(centers, bounds, position, np.array([-42, 48]), np.array([42, 78]), Path(name).stem + "-entry.png")


def compare(first, second, size):
    metrics = {}
    for layer in ("rays-direct", "rays-probe", "rays-ddgi", "rays-fog-input", "rays-fog", "display"):
        a, b = read_colors(first, layer), read_colors(second, layer)
        delta = np.abs(a - b)
        worst = int(np.argmax(delta.max(axis=1)))
        values = dict(entries=len(delta), mean_abs_rgb=float(delta.mean(dtype=np.float64)),
                      max_abs_rgb=float(delta.max()), entries_over_1e_4=int((delta.max(axis=1) > 1e-4).sum()))
        if delta.max() > 0:
            if layer == "rays-probe":
                values["worst_direction_index"] = worst
            else:
                values["worst_pixel_xy"] = [worst % size[0], worst // size[0]]
        if layer == "rays-fog":
            same_input = (read_colors(first, "rays-fog-input") == read_colors(second, "rays-fog-input")).all(axis=1)
            values.update(identical_input_pixels=int(same_input.sum()),
                          changed_outputs_for_identical_input=int((delta[same_input].max(axis=1) > 0).sum()))
        if layer == "display":
            codes = np.abs(display_bytes(a).astype(np.int16) - display_bytes(b)).max(axis=1)
            values.update(changed_rgb8_pixels=int(np.count_nonzero(codes)), max_rgb8_step=int(codes.max()))
        metrics[layer] = values
    return metrics


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executables", nargs="+", type=Path, help="Baseline first; include intermediate candidates in order")
    parser.add_argument("--tree-plans", action="store_true",
                        help="Draw the last two Sun Temple captures (absolute vertices, median then area splits)")
    args = parser.parse_args()
    versions = [sha256(path) for path in args.executables]
    assert len(set(versions)) == len(versions) >= 2
    OUT.mkdir(parents=True, exist_ok=True)
    runs, pixels = {}, {}
    for scene in ("Sun-Temple", "Sponza-Atrium"):
        runs[scene] = {}
        cases = [ROOT / "tmp/material-reference" / f"{scene}-rays-{v[:12]}-320" for v in versions]
        reference = None
        for version, case in zip(versions, cases):
            record = json.loads((case / "intersection.json").read_text(encoding="utf8"))
            scene_file = case / "assets/scenes/Sponza-Atrium.redflag"
            settings = Path(str(scene_file) + ".render.yaml")
            record.update(exe_sha256=version, scene_sha256=sha256(scene_file), settings_sha256=sha256(settings),
                          settings=yaml.safe_load(settings.read_text(encoding="utf8")),
                          fog_input_sha256=sha256(case / "rays-fog-input.f32"), display_sha256=sha256(case / "display.f32"))
            assert all(len(item["gpu_ms"]) == 11 for item in record["stages"].values())
            identity = [record[k] for k in ("scene_sha256", "settings_sha256", "frame", "draws", "dispatches")]
            # 同一组模型可以分成大小不同的包围盒。三角形数、镜头、灯与实际发出的工作组数仍要相同。
            identity += [record["scene"][k] for k in ("triangles", "size", "lights", "grid")]
            identity += [(s["groups"], s["parameters"]) for s in record["stages"].values()]
            if reference is None:
                reference = identity
            assert identity == reference, (scene, version)
            runs[scene][version[:12]] = record
        size = record["scene"]["size"]
        pixels[scene] = {}
        for offset, this in enumerate(cases):
            for other, version in zip(cases[offset+1:], versions[offset+1:]):
                pixels[scene][versions[offset][:12] + "_to_" + version[:12]] = compare(this, other, size)
        image = display_bytes(read_colors(cases[-1], "display")).reshape(size[1], size[0], 3)
        Image.fromarray(image).save(OUT / (scene + ".png"))
        if scene == "Sun-Temple" and args.tree_plans:
            for case, version, image_name in zip(cases[-2:], versions[-2:], ("tree-median.png", "tree-area.png")):
                tree_plan(case, runs[scene][version[:12]]["scene"]["root_children"], image_name)
    write_json("renderdoc.json", runs)
    write_json("pixels.json", pixels)


if __name__ == "__main__":
    main()
