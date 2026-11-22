"""从 Godot TPS 原模型选取维修场模块，保留原有几何、UV 与材质贴图。"""
from concurrent.futures import ThreadPoolExecutor
from copy import deepcopy
from io import BytesIO
import json
from pathlib import Path
import re
import struct
from time import sleep
from urllib.request import urlopen

from PIL import Image
from import_station_robots import ROOT, SOURCE, VERSION


OUT = ROOT / "RedflagForge/assets/game/station/yard"
CACHE = ROOT / "tmp/tps-art"
# 编号对应固定版本的 glTF meshes 数组。地面按最高点放置，其他部件按底面放置。
PARTS = {"Road": ("structure", 87, True), "Wall": ("structure", 27, False),
         "Pipework": ("structure", 26, False), "Door": ("structure", 79, False),
         "Container": ("props", 27, False), "Cargo": ("props", 14, False),
         "Floodlight": ("props", 38, False), "SteelBeam": ("props", 46, False),
         "GasBottles": ("props", 17, False), "Hose": ("props", 43, False)}


def fetch(name):
    path = CACHE / name
    if path.exists():
        return path
    path.parent.mkdir(parents=True, exist_ok=True)
    for attempt in range(4):
        try:
            with urlopen(SOURCE + name, timeout=40) as response:
                data = response.read()
            path.write_bytes(data)
            return path
        except OSError:
            if attempt == 3:
                raise
            sleep(1 + attempt)


def material_file(name):
    text = fetch("level/geometry/models/" + name + ".tres").read_text(encoding="utf8")
    paths = dict((i,p) for p,i in re.findall(r'path="res://([^"]+)" id="([^"]+)"', text))
    maps = {slot:paths[i] for slot,i in re.findall(r'^(\w+_texture) = ExtResource\("([^"]+)"\)', text,re.M)}
    return text, maps


def convert_map(name):
    source = fetch(name)
    with Image.open(source) as image:
        # 法线和金属度保持线性数据；这里只改变尺寸和编码，颜色空间由 glTF 的材质通道决定。
        image.thumbnail((1024,1024), Image.Resampling.LANCZOS)
        alpha = "A" in image.getbands() and image.getchannel("A").getextrema()[0] < 255
        image = image.convert("RGBA" if alpha else "RGB")
        target = OUT / "maps" / (source.stem + (".png" if alpha else ".jpg"))
        target.parent.mkdir(parents=True, exist_ok=True)
        image.save(target, quality=90)
    return "maps/" + target.name


def save_part(name, original, binary, index, floor, definitions, maps):
    mesh = deepcopy(original["meshes"][index])
    doc = dict(asset={"version":"2.0"},scene=0, scenes=[{"nodes":[0]}], nodes=[dict(mesh=0)],
               meshes=[mesh], buffers=[], bufferViews=[], accessors=[], materials=[], textures=[], images=[])
    packed = bytearray()
    indices, views, materials, images = {}, {}, {}, {}

    def accessor(old):
        if old not in indices:
            item = original["accessors"][old].copy()
            v = item["bufferView"]
            if v not in views:
                view = original["bufferViews"][v].copy()
                start, length = view.get("byteOffset",0), view["byteLength"]
                packed.extend(bytes(-len(packed)%4))
                view.update(buffer=0,byteOffset=len(packed))
                views[v] = len(doc["bufferViews"])
                doc["bufferViews"].append(view)
                packed.extend(binary[start:start+length])
            item["bufferView"] = views[v]
            indices[old] = len(doc["accessors"])
            doc["accessors"].append(item)
        return indices[old]

    def add_map(src):
        if src not in images:
            images[src] = len(doc["images"])
            doc["images"].append(dict(uri=maps[src]))
            doc["textures"].append(dict(source=images[src]))
        return dict(index=images[src])

    limits = []
    for p in mesh["primitives"]:
        position = original["accessors"][p["attributes"]["POSITION"]]
        limits.append((position["min"],position["max"]))
        p["attributes"] = {k:accessor(a) for k,a in p["attributes"].items()}
        if "indices" in p: p["indices"] = accessor(p["indices"])
        if "material" not in p: continue
        old = p["material"]
        if old not in materials:
            mat = deepcopy(original["materials"][old])
            text, textures = definitions[mat["name"]]
            pbr = mat.setdefault("pbrMetallicRoughness", {})
            if "albedo_texture" in textures:
                pbr.update(baseColorTexture=add_map(textures["albedo_texture"]),baseColorFactor=[1]*4)
            if "roughness_texture" in textures:
                pbr.update(metallicRoughnessTexture=add_map(textures["roughness_texture"]),roughnessFactor=1,
                           metallicFactor=1 if "metallic_texture" in textures else 0)
            for src,dst in (("normal_texture","normalTexture"),("ao_texture","occlusionTexture"),("emission_texture","emissiveTexture")):
                if src in textures: mat[dst] = add_map(textures[src])
            if "emissiveTexture" in mat: mat["emissiveFactor"] = [1,1,1]
            if "transparency = 4" in text or "transparency = 2" in text: mat.update(alphaMode="MASK",alphaCutoff=.5)
            materials[old] = len(doc["materials"])
            doc["materials"].append(mat)
        p["material"] = materials[old]
    lo = [min(b[0][k] for b in limits) for k in range(3)]
    hi = [max(b[1][k] for b in limits) for k in range(3)]
    doc["nodes"][0]["translation"] = [-(lo[0]+hi[0])/2, -hi[1] if floor else -lo[1], -(lo[2]+hi[2])/2]
    doc["buffers"].append(dict(uri=name+".bin",byteLength=len(packed)))
    (OUT/(name+".gltf")).write_text(json.dumps(doc,separators=(",",":")),encoding="utf8")
    (OUT/(name+".bin")).write_bytes(packed)


def main():
    OUT.mkdir(parents=True,exist_ok=True)
    libraries, names = {}, set()
    for group,index,_ in PARTS.values():
        if group not in libraries:
            blob = fetch("level/geometry/models/"+group+".glb").read_bytes()
            n = struct.unpack_from("<I",blob,12)[0]
            libraries[group] = json.loads(blob[20:20+n]), blob[28+n:]
        library = libraries[group][0]
        names.update(library["materials"][p["material"]]["name"] for p in library["meshes"][index]["primitives"] if "material" in p)
    with ThreadPoolExecutor(max_workers=5) as pool:
        names = sorted(names)
        definitions = dict(zip(names,pool.map(material_file,names)))
        textures = sorted({p for _,slots in definitions.values() for p in slots.values()})
        maps = dict(zip(textures,pool.map(convert_map,textures)))
    for name,(group,index,floor) in PARTS.items():
        save_part(name,*libraries[group],index,floor,definitions,maps)
    (OUT/"sources.json").write_text(json.dumps(dict(repository="https://github.com/godotengine/tps-demo",commit=VERSION,
        license="CC-BY-3.0",authors=["Juan Linietsky","Fernando Miguel Calabró"],meshes=PARTS,textures=maps),indent=2),encoding="utf8")


if __name__ == "__main__":
    main()
