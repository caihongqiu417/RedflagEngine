"""把作者的底色、切线法线和高度图烘成游戏使用的分辨率。配方保存来源、校验值和目标尺寸。"""
import argparse
import hashlib
import json
from pathlib import Path
import tempfile
from urllib.request import Request, urlopen
import shutil

import numpy as np
from PIL import Image


def digest(path, algorithm="sha256"):
    with path.open("rb") as source:
        return hashlib.file_digest(source, algorithm).hexdigest()


def source_image(entry, cache):
    cache.mkdir(parents=True, exist_ok=True)
    if Path(entry["file"]).name != entry["file"]:
        raise ValueError("源图文件名不能包含目录。")
    path = cache / entry["file"]
    if not path.exists():
        request = Request(entry["url"], headers={"User-Agent": "Redflag material authoring"})
        with tempfile.TemporaryDirectory(dir=cache) as temporary:
            download = Path(temporary) / "source"
            with urlopen(request, timeout=120) as source, download.open("wb") as file:
                shutil.copyfileobj(source, file)
            if digest(download, "md5") != entry["md5"]:
                raise ValueError(f"下载内容和作者记录不符：{path.name}")
            download.replace(path)
    if digest(path, "md5") != entry["md5"]:
        raise ValueError(f"缓存的源图与配方不符：{path}")
    return path


def bake_image(source, kind, size):
    with Image.open(source) as image:
        if kind == "height":
            if image.mode not in ("L", "I;16", "I;16B"):
                raise ValueError("高度应来自单通道的 8 位或 16 位图像。")
            pixels = image.copy()
        else:
            if ("A" in image.getbands() or "transparency" in image.info) and image.convert("RGBA").getchannel("A").getextrema()[0] < 255:
                raise ValueError("此配方用于不透明材质，输入图片含有透明区域。")
            pixels = image.convert("RGB")
    width, height = pixels.size
    out_width, out_height = size
    if min(size) < 1 or width % out_width or height % out_height:
        raise ValueError("目标尺寸应能整除源图尺寸；放大低清图不会增加细节。")
    dx, dy = width // out_width, height // out_height
    channels = 1 if kind == "height" else 3
    result = np.empty((out_height, out_width, channels), dtype=np.uint8)

    # 一次处理 64 行输出，4K 扫描也无需同时保留数份整图浮点数组。
    for start in range(0, out_height, 64):
        end = min(start + 64, out_height)
        strip = pixels.crop((0, start * dy, width, end * dy))
        full_scale = 65535 if pixels.mode.startswith("I;16") else 255
        data = np.asarray(strip, dtype=np.float32) / full_scale
        if kind == "base_color":
            data = np.where(data <= .04045, data / 12.92, ((data + .055) / 1.055) ** 2.4)
        elif kind == "normal_gl":
            data = data * 2 - 1
            data /= np.maximum(np.linalg.norm(data, axis=2, keepdims=True), 1e-8)

        filtered = data.reshape(end-start, dy, out_width, dx, channels).mean(axis=(1, 3))
        if kind == "base_color":
            filtered = np.where(filtered <= .0031308, 12.92 * filtered, 1.055 * filtered ** (1 / 2.4) - .055)
        elif kind == "normal_gl":
            length = np.linalg.norm(filtered, axis=2, keepdims=True)
            # 相反的法线互相抵消时，用平面的朝向表示这块纹素。
            filtered = np.where(length > 1e-6, filtered / np.maximum(length, 1e-6), [0, 0, 1])
            filtered = filtered * .5 + .5
        result[start:end] = np.rint(np.clip(filtered, 0, 1) * 255).astype(np.uint8)
    return Image.fromarray(result[:, :, 0] if kind == "height" else result)


def bake(recipe_path, cache, destination):
    recipe = json.loads(recipe_path.read_bytes())
    names = {"base_color": "BaseColor.jpg", "normal_gl": "Normal.png", "height": "Height.png"}
    # Git 在 Windows 上可能换成 CRLF；只要配方内容相同，就应认出同一份资产。
    canonical = json.dumps(recipe, sort_keys=True, separators=(",", ":")).encode("utf8")
    recipe_hash = hashlib.sha256(canonical).hexdigest()
    if destination.exists():
        receipt = json.loads((destination / "sources.json").read_bytes())
        outputs = receipt["outputs"]
        if (receipt.get("baker") == 1 and receipt["recipe_sha256"] == recipe_hash and set(outputs) == {names[k] for k in recipe["maps"]}
                and all(digest(destination / name) == sha for name, sha in outputs.items())):
            return receipt
        raise ValueError("配方或已有贴图已改变。请选择新目录制作对照，原资产仍可使用。")

    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=destination.parent) as temp:
        stage = Path(temp) / "maps"
        stage.mkdir()
        outputs = {}
        sources = {}
        for kind, entry in recipe["maps"].items():
            name = names[kind]
            source = source_image(entry, cache)
            image = bake_image(source, kind, recipe["size"])
            options = {"quality": 94, "subsampling": 0} if kind == "base_color" else {}
            image.save(stage / name, **options)
            outputs[name] = digest(stage / name)
            sources[kind] = dict(entry, sha256=digest(source))
        receipt = dict(baker=1, source=recipe["source"], license=recipe["license"], size=recipe["size"],
                       recipe_sha256=recipe_hash, inputs=sources, outputs=outputs)
        (stage / "sources.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf8")
        stage.rename(destination)
    return receipt


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("recipe", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--cache", type=Path, default=Path("tmp/material-sources"))
    args = parser.parse_args()
    print(json.dumps(bake(args.recipe, args.cache, args.destination), indent=2))
