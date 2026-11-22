"""从已有的两三角形方片生成相机、遮挡和倒影三组测试共用的 glTF/PNG。"""
import json
from pathlib import Path

from PIL import Image

work = Path("bin-int/Tests/gi-mask")
work.mkdir(parents=True, exist_ok=True)
quad = json.loads((Path(__file__).parent / "fixtures/TextureQuad.gltf").read_text(encoding="utf8"))
quad["textures"] = [{"source": 0}]
quad["images"] = [{"uri": "mask.png"}]
texture = Image.new("RGBA", (16, 16), (42, 180, 68, 255))
texture.paste((42, 180, 68, 0), (5, 5, 11, 11))
texture.save(work / "mask.png")
# 透明度只放红通道，绿、蓝和 alpha 刻意填入不同的值。
opacity = Image.new("RGBA", (16, 16), (255, 0, 255, 0))
opacity.paste((0, 255, 0, 255), (5, 5, 11, 11))
opacity.save(work / "opacity.png")
Image.new("RGBA", (1, 1), (128, 255, 0, 0)).save(work / "half-red.png")

# OBJ 单独保存透明度图的路径，glTF 则将透明度放在底图的 alpha 通道。
(work / "fence.mtl").write_text("newmtl fence\nKd 1 1 1\nmap_d opacity.png\n", encoding="ascii")
(work / "fence.obj").write_text(
    "mtllib fence.mtl\no Fence\nv -1 1 0\nv 1 1 0\nv -1 -1 0\nv 1 -1 0\n"
    "vt 0 0\nvt 1 0\nvt 0 1\nvt 1 1\nusemtl fence\nf 1/1 3/3 2/2\nf 2/2 3/3 4/4\n",
    encoding="ascii")

for name in ("receiver", "mask", "opaque"):
    material = {"pbrMetallicRoughness": {"baseColorFactor": [1, 1, 1, 1], "roughnessFactor": .8,
                                          "metallicFactor": 0}, "doubleSided": True}
    if name != "receiver":
        material["alphaMode"] = name.upper()
        material["pbrMetallicRoughness"]["baseColorTexture"] = {"index": 0}
    if name == "mask":
        material["alphaCutoff"] = .5
    quad["materials"] = [material]
    (work / (name + ".gltf")).write_text(json.dumps(quad, indent=2), encoding="utf8")
