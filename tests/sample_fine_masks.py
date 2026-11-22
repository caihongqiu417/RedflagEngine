"""在 Sponza 源图每个像素的中心，比较 CPU 从截帧字节算出的透明度和源图。"""
import hashlib
import json
from pathlib import Path

import numpy as np
from PIL import Image


def materials(root):
    folder = root / "RedflagForge/assets/models/reference/Sponza"
    model = json.loads((folder / "Sponza.gltf").read_bytes())
    result = {}
    for i, mat in enumerate(model["materials"]):
        if mat.get("alphaMode") != "MASK":
            continue
        tex = mat["pbrMetallicRoughness"]["baseColorTexture"]["index"]
        image = folder / model["images"][model["textures"][tex]["source"]]["uri"]
        with Image.open(image) as bitmap:
            rgba = np.array(bitmap.convert("RGBA"))[::-1]
        assert rgba.shape == (1024, 1024, 4)
        # GI 的 512 图从原图每个 2×2 方格中取右上方的像素（从左下角开始编号）。
        key = hashlib.sha256(rgba[1::2, 1::2].tobytes()).digest()
        result[key] = (dict(material=i, file=image.name, sha256=hashlib.sha256(image.read_bytes()).hexdigest(),
                            width=rgba.shape[1], height=rgba.shape[0], cutoff=mat.get("alphaCutoff", .5)), rgba[:, :, 3])
    return result


def sample_centers(alpha, size):
    # 对源图的每个像素中心，在实际遮罩中找四个邻居，权重与 GLSL sampleMask 相同。
    h, w = alpha.shape
    width, height = size
    x = (np.arange(width, dtype=np.float32) + .5) * (w / width) - .5
    y = (np.arange(height, dtype=np.float32) + .5) * (h / height) - .5
    i, j = np.floor(x).astype(int), np.floor(y).astype(int)
    u, v = x - i, (y - j)[:, None]
    top = (1-u) * alpha[np.ix_(j % h, i % w)] + u * alpha[np.ix_(j % h, (i+1) % w)]
    bottom = (1-u) * alpha[np.ix_((j+1) % h, i % w)] + u * alpha[np.ix_((j+1) % h, (i+1) % w)]
    return (1-v) * top + v * bottom


def check(folder, source):
    triangles = np.fromfile(folder / "triangles.bin", "<f4").reshape(-1, 16)
    faces = np.fromfile(folder / "faces.bin", "<f4").reshape(-1, 16)
    pixels = np.fromfile(folder / "material_pixels.bin", "<u4")
    masks = np.fromfile(folder / "masks.bin", "<f4").reshape(-1, 8)
    assert np.all(masks[:, 0] == 1) and np.all(masks[:, 1] == .5)
    assert not masks[:, 4:].view("<u4").any()
    indices = -triangles[triangles[:, 15] < 0, 15].astype(int) - 1
    descriptors = faces[indices, 8:12].view("<u4")
    rows, images = [], {}
    for desc, count in zip(*np.unique(descriptors, axis=0, return_counts=True)):
        offset, w, h, fine = map(int, desc)
        color = pixels[offset:offset + w*h]
        meta, ref = source[hashlib.sha256(color.tobytes()).digest()]
        if fine:
            w, h = map(int, pixels[fine-1:fine+1])
            start = (fine + 1) * 4
            alpha = pixels.view(np.uint8)[start:start+w*h].reshape(h, w)
        else:
            alpha = (color >> 24).astype(np.uint8).reshape(h, w)
        threshold = meta["cutoff"] * 255
        samples = sample_centers(alpha, (meta["width"], meta["height"])) >= threshold
        expected = ref >= threshold
        mask_rows = faces[indices[np.all(descriptors == desc, axis=1)], 7].astype(int)
        row = dict(meta, triangles=int(count), mask_rows=np.unique(mask_rows).tolist(),
                   stored_size=[w, h], sampled_centers=int(samples.size),
                   stored_alpha_sha256=hashlib.sha256(alpha.tobytes()).hexdigest(),
                   source_alpha_sha256=hashlib.sha256(ref.tobytes()).hexdigest(),
                   passed_through_solid=int((~samples & expected).sum()),
                   blocked_holes=int((samples & ~expected).sum()))
        rows.append(row)
        images[meta["material"]] = (expected, samples)
    assert {row["material"] for row in rows} == {0, 3, 20}
    return dict(triangles=len(triangles), masked_triangles=len(indices), mask_rows=len(masks),
                materials=sorted(rows, key=lambda row: row["material"])), images


def compare(root, work, output):
    source = materials(root)
    results, images = {}, {}
    for name in ("old", "new"):
        results[name], images[name] = check(work / name, source)
    for a, b in zip(results["old"]["materials"], results["new"]["materials"]):
        assert a["material"] == b["material"] and a["mask_rows"] == b["mask_rows"]
        assert b["stored_alpha_sha256"] == b["source_alpha_sha256"]
        assert a["passed_through_solid"] + a["blocked_holes"] and not (b["passed_through_solid"] + b["blocked_holes"])
        i = a["material"]
        ref, old = images["old"][i]
        new = images["new"][i][1]
        # 展示每张图中错判最多的 64×64 方格，坐标记入数据文件，可回到源图核对。
        diff = (ref != old).reshape(16, 64, 16, 64).sum(axis=(1, 3))
        y, x = np.unravel_index(diff.argmax(), diff.shape)
        x, y = int(x) * 64, int(y) * 64
        a["crop_bottom_left"] = [x, y]
        if i == 0:
            crops = [img[y:y+64, x:x+64][::-1] for img in (ref, old, new)]
    # 两种醒目的颜色专门标出错判处，普通的深浅颜色表示这里挡光或透光。
    sheet = Image.new("RGB", (3*192+2*6, 192), "white")
    for col, crop in enumerate(crops):
        color = np.where(crop[..., None], [40, 58, 57], [232, 242, 237]).astype(np.uint8)
        color[crop & ~crops[0]] = [170, 46, 71]
        color[~crop & crops[0]] = [232, 157, 48]
        sheet.paste(Image.fromarray(color).resize((192, 192), Image.Resampling.NEAREST), (col*198, 0))
    sheet.save(output / "mask-centers.png")
    measure = dict(reference="Source baseColor PNG alpha >= 0.5, each original texel center, LOD 0, repeat addressing. "
                             "CPU calculation from RenderDoc buffer bytes; no counts of scene pixels or rays.",
                   crop="Material 0, 64x64 nonoverlapping tile with most old misclassifications. Columns: source, "
                        "old GPU buffer sampled on CPU, new GPU buffer sampled on CPU. Red blocks a hole, amber lets light through solid.",
                   cases=results)
    (output / "mask-centers.json").write_text(json.dumps(measure, indent=2) + "\n", encoding="utf8")
