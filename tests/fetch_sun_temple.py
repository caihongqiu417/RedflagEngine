"""Prepare Epic Games' ORCA Sun Temple for Redflag. Requires Pillow, NumPy and Blender 5."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
from io import BytesIO
from pathlib import Path
import shutil
import subprocess
from urllib.request import urlopen
from zipfile import ZipFile

import numpy as np
from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
SOURCE = "https://developer.nvidia.com/sun-temple"
SHA256 = "672b2ee05f5c7f3b51b56602d7ffe1cb2ce17a7df4038109dda942872aec16ac"
PREFIX = "SunTemple_v4/SunTemple/"
TARGET = ROOT / "RedflagForge/assets/models/reference/SunTemple"


def prepare(archive, blender):
    candidate = archive
    if not archive.exists():
        archive.parent.mkdir(parents=True, exist_ok=True)
        candidate = archive.with_suffix(".part")
        with urlopen(SOURCE, timeout=120) as incoming, candidate.open("wb") as outgoing:
            shutil.copyfileobj(incoming, outgoing)
    with candidate.open("rb") as stream:
        if hashlib.file_digest(stream, "sha256").hexdigest() != SHA256:
            raise ValueError(f"Unexpected SunTemple v4 archive: {candidate}")
    if candidate != archive:
        candidate.replace(archive)

    textures = TARGET / "Textures"
    textures.mkdir(parents=True, exist_ok=True)
    fbx = archive.parent / "SunTemple.fbx"
    with ZipFile(archive) as scene:
        fbx.write_bytes(scene.read(PREFIX + fbx.name))
        for name in ("LICENSE.txt", "README.txt", "CHANGELOG.txt"):
            (TARGET / name).write_bytes(scene.read(PREFIX + name))

        def convert(name):
            image = Image.open(BytesIO(scene.read(name)))
            image.thumbnail((1024, 1024), Image.Resampling.LANCZOS)
            if name.endswith("_Normal.dds"):
                # BC5 holds DirectX tangent-space X and Y. glTF uses the opposite Y axis.
                xy = np.asarray(image, dtype=np.float32)[:, :, :2] / 127.5 - 1
                xy[:, :, 1] *= -1
                z = np.sqrt(np.maximum(1 - (xy ** 2).sum(axis=2), 0))
                normal = np.dstack((xy, z))
                normal /= np.linalg.norm(normal, axis=2, keepdims=True)
                image = Image.fromarray(np.rint((normal + 1) * 127.5).astype(np.uint8))
            image.save(textures / Path(name).with_suffix(".png").name)

        with ThreadPoolExecutor(max_workers=4) as pool:
            list(pool.map(convert, (n for n in scene.namelist() if n.endswith(".dds"))))

    model = TARGET / "SunTemple.gltf"
    subprocess.run([blender, "-b", "--factory-startup", "--python-exit-code", "1", "-P",
                    str(ROOT / "tests/support/export_sun_temple.py"), "--", str(fbx), str(model)], check=True)
    print(model)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, default=ROOT / "tmp/orca/SunTemple_v4.zip")
    parser.add_argument("--blender", default="blender")
    args = parser.parse_args()
    prepare(args.archive.resolve(), args.blender)
