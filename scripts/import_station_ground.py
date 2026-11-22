"""把项目的一米平面和 Poly Haven 扫描的路面材质一起导入，按实物尺寸铺设纹理。"""
from array import array
from io import BytesIO
import json
from pathlib import Path
from PIL import Image

from fetch_station_props import OUT, ROOT, verified, read_url


def main():
    asset = "gravel_road"
    files = json.loads(read_url("https://api.polyhaven.com/files/" + asset))
    source = files["gltf"]["2k"]["gltf"]
    material = json.loads(verified(source))
    folder = OUT / "ground"
    folder.mkdir(parents=True, exist_ok=True)
    index = material["materials"][0]["pbrMetallicRoughness"]["metallicRoughnessTexture"]["index"]
    packed_image = material["images"][material["textures"][index]["source"]]
    for image in material["images"]:
        uri = image["uri"]
        image["uri"] = Path(uri).name
        pixels = verified(source["include"][uri])
        if image is packed_image:
            packed = Image.open(BytesIO(pixels))
        else:
            (folder / image["uri"]).write_bytes(pixels)

    # 这份下载包的合图里粗糙度几乎全黑。使用作者单独发布的 Rough 图恢复砂石表面。
    roughness = files["Rough"]["2k"]["jpg"]
    rough = Image.open(BytesIO(verified(roughness))).convert("L")
    corrected = Image.merge("RGB", (packed.getchannel("R"), rough, packed.getchannel("B")))
    packed_image.update(uri="gravel_road_orm_2k.png", mimeType="image/png")
    corrected.save(folder / packed_image["uri"])

    # 源材质预览用的是球。场景用项目已有的平面，并保留作者的颜色、法线和粗糙度通道。
    plane = ROOT / "RedflagForge/assets/models/Primitives/Plane.obj"
    rows = [line.split() for line in plane.read_text().splitlines() if line and not line.startswith("#")]
    positions = [list(map(float, r[1:])) for r in rows if r[0] == "v"]
    uv = [list(map(float, r[1:])) for r in rows if r[0] == "vt"]
    faces = [int(v.split("/")[0])-1 for r in rows if r[0] == "f" for v in r[1:]]
    # 场景中的地面宽 100 米，作者扫描了 2 米宽的碎石。把 UV 按这两个尺寸换算。
    metres = 100
    vertices = array("f", (x for p in positions for x in p))
    texcoords = array("f", (x*metres/2 for st in uv for x in st))
    indices = array("H", faces)
    blocks = [vertices.tobytes(), texcoords.tobytes(), indices.tobytes()]
    blob = b"".join(blocks)
    (folder / "Gravel.bin").write_bytes(blob)
    material.update(nodes=[{"mesh":0}], scenes=[{"nodes":[0]}], scene=0,
                    meshes=[{"primitives":[{"attributes":{"POSITION":0,"TEXCOORD_0":1},"indices":2,"material":0}]}],
                    buffers=[{"uri":"Gravel.bin","byteLength":len(blob)}],
                    bufferViews=[{"buffer":0,"byteOffset":sum(map(len,blocks[:i])),"byteLength":len(data)} for i,data in enumerate(blocks)],
                    accessors=[{"bufferView":0,"componentType":5126,"count":len(positions),"type":"VEC3",
                                "min":list(map(min,zip(*positions))),"max":list(map(max,zip(*positions)))},
                               {"bufferView":1,"componentType":5126,"count":len(uv),"type":"VEC2"},
                               {"bufferView":2,"componentType":5123,"count":len(faces),"type":"SCALAR"}])
    (folder/"Gravel.gltf").write_text(json.dumps(material,indent=2),encoding="utf8")
    source_info = dict(asset=asset,author="Amal Kumar",license="CC0-1.0",source="https://polyhaven.com/a/gravel_road",
                       material_md5=source["md5"],roughness_md5=roughness["md5"],
                       geometry=plane.relative_to(ROOT).as_posix(),width_m=metres,scan_m=2)
    (folder/"sources.json").write_text(json.dumps(source_info,indent=2),encoding="utf8")


if __name__ == "__main__":
    main()
