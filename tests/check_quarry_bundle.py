"""从 Watt Station 的实际资产包复查地形依赖，在解包目录用 C++ 序列化器读写地形实体。"""
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory
from zipfile import ZipFile

from ruamel.yaml import YAML

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / "RedflagForge"
sys.path.insert(0, str(ROOT / "scripts"))
from asset_bundle import AssetBundle


def check(work):
    yaml = YAML(typ="safe")
    bundle = AssetBundle(PROJECT)
    name = bundle.scene(PROJECT / "assets/scenes/Watt-Station.redflag")
    archive = work / "station.zip"
    bundle.write(archive, name)

    with ZipFile(archive) as game:
        scene = yaml.load(game.read(name))
        terrain, = [row for row in scene["Entities"] if "TerrainComponent" in row]
        fields = terrain["TerrainComponent"]
        manifest = {row["path"]: row for row in json.loads(game.read("asset-manifest.json"))["files"]}
        for key in ("Heightmap", "BaseColorTexture"):
            path = fields[key]
            assert name in manifest[path]["used_by"], (key, path)
            image = game.read(path)
            assert hashlib.sha256(image).hexdigest() == manifest[path]["sha256"]
            assert image == (PROJECT / path).read_bytes(), path
            game.extract(path, work)
        assert "assets/terrain/quarry/README.md" in manifest

    # 沿用包中实体的全部字段，在只含这两张图的目录里读写，避免误读开发目录里的原图。
    source = work / "ground.redflag"
    saved = work / "saved.redflag"
    with source.open("w", encoding="utf-8") as stream:
        yaml.dump({"Scene": "Quarry", "Entities": [terrain]}, stream)
    env = dict(os.environ)
    env["PATH"] = str(ROOT / "bin/Debug-windows-x86_64/RedflagForge") + os.pathsep + env["PATH"]
    test = ROOT / "bin-int/Tests/SceneRenderTests.exe"
    subprocess.run([test, "--terrain-resave", source.name, saved.name], check=True, cwd=work, env=env)

    actual = yaml.load(saved.read_bytes())["Entities"][0]
    assert actual["Entity"] == terrain["Entity"]
    restored = actual["TerrainComponent"]
    assert set(restored) == set(fields), (restored.keys(), fields.keys())
    for key, original in fields.items():
        value = restored[key]
        if isinstance(original, list):
            assert len(original) == len(value) and all(math.isclose(x,y,abs_tol=1e-6) for x,y in zip(original,value)), key
        elif isinstance(original, float):
            assert math.isclose(value, original, abs_tol=1e-6), key
        else:
            assert value == original, key
    print(f"PASS Watt Station archive: both original images, credit, and {len(fields)} serialized terrain fields")


if __name__ == "__main__":
    with TemporaryDirectory() as work:
        check(Path(work))
