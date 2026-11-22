"""把 Godot 官方 TPS 样例的角色和原版 PBR 贴图导入 Redflag，无需安装 Godot。"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess


ROOT = Path(__file__).resolve().parents[1]
VERSION = "a82f15448e9b015440d3bbdf5e10801b260c4e9f"
SOURCE = "https://raw.githubusercontent.com/godotengine/tps-demo/" + VERSION + "/"
DEST = ROOT / "RedflagForge/assets/game/station/crew"


def source_file(cache, name):
    path = cache / name
    if not path.is_file():
        path.parent.mkdir(parents=True, exist_ok=True)
        temp = path.with_suffix(path.suffix + ".download")
        subprocess.run(["curl", "--fail", "--location", "--silent", "--show-error",
                        "--retry", "4", "--retry-all-errors", SOURCE + name, "-o", str(temp)], check=True)
        temp.replace(path)
    return path


def import_robot(cache, enemy, motion, floor_motion):
    folder = "enemies/red_robot/" if enemy else "player/"
    name = "red_robot" if enemy else "player"
    file = source_file(cache, folder + "model/" + name + ".glb")
    data = file.read_bytes()
    size = struct.unpack_from("<I", data, 12)[0]
    doc = json.loads(data[20:20+size])
    geometry = data[28+size:]
    if len(data) != struct.unpack_from("<I", data, 8)[0]:
        raise ValueError("角色文件下载不完整")

    # 样例在 Godot 材质文件中指定贴图，原 GLB 只带颜色。将同一套图恢复为 glTF 材质。
    texture_names = ["albedo", "normal", "orm", "emission" if enemy else "emissive"]
    prefix = folder + "textures/" + ("red_robot_" if enemy else "player_robot_")
    doc["images"], doc["textures"] = [], []
    for i, label in enumerate(texture_names):
        pixels = source_file(cache, prefix + label + ".png").read_bytes()
        view = len(doc["bufferViews"])
        doc["bufferViews"].append(dict(buffer=0, byteOffset=0, byteLength=len(pixels)))
        doc["images"].append(dict(name=label, mimeType="image/png", bufferView=view))
        doc["textures"].append(dict(source=i))
        doc["bufferViews"][view]["byteOffset"] = len(geometry)
        geometry += pixels + bytes(-len(pixels) % 4)

    paint = doc["materials"][0]
    paint["pbrMetallicRoughness"] = dict(baseColorTexture=dict(index=0),metallicRoughnessTexture=dict(index=2),
                                         metallicFactor=1,roughnessFactor=1)
    paint.update(normalTexture=dict(index=1),occlusionTexture=dict(index=2),emissiveTexture=dict(index=3),emissiveFactor=[1,1,1])
    if not enemy:
        doc["materials"][1]["emissiveFactor"] = [.196, .388, .639]

    # 丢弃作者命名为 noexp 的试作剪辑，保留游戏里使用的臂部装备切换。
    doc["animations"] = [clip for clip in doc["animations"] if "noexp" not in clip["name"].lower()
                         or clip["name"] == "Cannon_activation-noexp"]
    if not enemy:
        from retarget_station_motion import add_interactions
        geometry = add_interactions(doc, geometry, motion, floor_motion)
    doc["buffers"][0]["byteLength"] = len(geometry)
    header = json.dumps(doc, separators=(",", ":")).encode()
    header += b" " * (-len(header) % 4)
    total = 28 + len(header) + len(geometry)
    glb = struct.pack("<4sIIII", b"glTF",2,total,len(header),0x4e4f534a) + header
    glb += struct.pack("<I4s",len(geometry),b"BIN\0") + geometry
    target = DEST / ("Sentinel.glb" if enemy else "Operator.glb")
    target.write_bytes(glb)
    record = dict(file=target.name, upstream=folder + "model/" + name + ".glb", commit=VERSION,
                  original_sha256=hashlib.sha256(data).hexdigest(), sha256=hashlib.sha256(glb).hexdigest())
    if not enemy:
        record["additional_motion"] = [
            dict(author="Quaternius", license="CC0-1.0", source="https://quaternius.com/packs/" + pack + ".html",
                 file=path.name, sha256=hashlib.sha256(path.read_bytes()).hexdigest(), original=clip, imported=name)
            for path, pack, clip, name in [(floor_motion, "universalanimationlibrary2", "Farm_Harvest", "Station-Pickup"),
                                            (motion, "universalanimationlibrary", "Interact", "Station-Give")]]
    return record


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache", type=Path, default=ROOT / "tmp/tps-art")
    parser.add_argument("--animations", type=Path, default=ROOT / "tmp/animation-sources/UAL1_Standard.glb",
                        help="作者 Universal Animation Library 标准包的 Unreal-Godot/UAL1_Standard.glb")
    parser.add_argument("--floor-animations", type=Path, default=ROOT / "tmp/animation-sources/UAL2_Standard.glb",
                        help="第 2 册标准包的 Unreal-Godot/UAL2_Standard.glb")
    args = parser.parse_args()
    DEST.mkdir(parents=True, exist_ok=True)
    sources = [import_robot(args.cache, enemy, args.animations, args.floor_animations) for enemy in (False, True)]
    shutil.copyfile(source_file(args.cache, "LICENSE.md"), DEST / "LICENSE-Godot.md")
    (DEST / "sources.json").write_text(json.dumps(sources, indent=2) + "\n", encoding="utf8")
