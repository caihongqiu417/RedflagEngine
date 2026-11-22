"""下载维修站的实物扫描模型，将 glTF 和贴图合成可以直接导入引擎的 GLB。"""
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import struct
import time
from urllib.request import Request, urlopen


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "RedflagForge/assets/game/station/salvage"
CACHE = ROOT / "tmp/station-assets"
MODELS = ("portable_generator", "metal_tool_chest", "rusted_wheel_rim_01",
          "metal_jerrycan", "barrel_03", "worn_metal_rack", "overhead_crane",
          "hanging_industrial_lamp", "industrial_storage_cart")


def read_url(url):
    for attempt in range(3):
        try:
            with urlopen(Request(url, headers={"User-Agent": "RedflagEngine asset importer"}), timeout=50) as response:
                return response.read()
        except OSError:
            if attempt == 2:
                raise
            time.sleep(1 + attempt)


def verified(file):
    path = CACHE / file["md5"]
    if not path.exists():
        data = read_url(file["url"])
        if hashlib.md5(data).hexdigest() != file["md5"]:
            raise ValueError("下载不完整 " + file["url"])
        path.write_bytes(data)
    return path.read_bytes()


def pack(name):
    sources = json.loads(read_url("https://api.polyhaven.com/files/" + name))
    source = sources["gltf"]["1k"]["gltf"]
    doc = json.loads(verified(source))
    files = source["include"]
    binary = bytearray(verified(files[doc["buffers"][0].pop("uri")]))

    # glTF 的图片原本各自存放。GLB 把它们放在同一段二进制数据内，材质仍引用原来的图片编号。
    for image in doc.get("images", []):
        uri = image.pop("uri")
        pixels = verified(files[uri])
        binary.extend(bytes(-len(binary) % 4))
        image["bufferView"] = len(doc["bufferViews"])
        image["mimeType"] = "image/png" if uri.lower().endswith(".png") else "image/jpeg"
        doc["bufferViews"].append(dict(buffer=0, byteOffset=len(binary), byteLength=len(pixels)))
        binary.extend(pixels)

    doc["buffers"][0]["byteLength"] = len(binary)
    header = json.dumps(doc, separators=(",", ":")).encode()
    header += b" " * (-len(header) % 4)
    binary.extend(bytes(-len(binary) % 4))
    glb = struct.pack("<4sIIII", b"glTF", 2, 28 + len(header) + len(binary), len(header), 0x4e4f534a)
    glb += header + struct.pack("<I4s", len(binary), b"BIN\0") + binary
    (OUT / (name + ".glb")).write_bytes(glb)
    return dict(asset=name, source="https://polyhaven.com/a/" + name, license="CC0-1.0",
                gltf_md5=source["md5"], sha256=hashlib.sha256(glb).hexdigest())


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    CACHE.mkdir(parents=True, exist_ok=True)
    with ThreadPoolExecutor(max_workers=3) as pool:
        entries = list(pool.map(pack, MODELS))
    sky = "industrial_sunset_02"
    files = json.loads(read_url("https://api.polyhaven.com/files/" + sky))
    source = files["hdri"]["2k"]["hdr"]
    image = verified(source)
    (OUT / (sky + ".hdr")).write_bytes(image)
    entries.append(dict(asset=sky, source="https://polyhaven.com/a/" + sky, license="CC0-1.0",
                        sha256=hashlib.sha256(image).hexdigest()))
    (OUT / "sources.json").write_text(json.dumps(entries, indent=2) + "\n", encoding="utf8")


if __name__ == "__main__":
    main()
