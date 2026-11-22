"""Download the Antique Camera and the studio panorama used by Camera-Studio.redflag."""
from hashlib import sha256
from pathlib import Path
from tempfile import NamedTemporaryFile
from urllib.request import urlopen

ASSETS = Path(__file__).resolve().parents[1] / "RedflagForge/assets"
MODEL = "https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/e124e8d8d7330a5372bc2b4d00d39a4a9d7997ea/Models/AntiqueCamera"
HDR = "https://dl.polyhaven.org/file/ph-assets/HDRIs/hdr/1k/studio_small_09_1k.hdr"
FILES = (
    (MODEL + "/glTF-Binary/AntiqueCamera.glb", "models/reference/AntiqueCamera/AntiqueCamera.glb",
     "ae89695479ad997f09ebea8060a5ad01312ad8fe7ffb92f73b4e895109feecd2"),
    (MODEL + "/LICENSE.md", "models/reference/AntiqueCamera/LICENSE.md",
     "ea9b3641260ec5d67804e7b85ed8e7ca6f71bd614bee7275bce41ff00c69c9d7"),
    (HDR, "textures/studio_small_09_1k.hdr",
     "e7cfda5f4e98e623db12b8bfd0184e048488e4855d9c83e2751fb44a32e80c45"),
)


def fetch(url, path, digest):
    if path.is_file() and sha256(path.read_bytes()).hexdigest() == digest:
        return
    with urlopen(url, timeout=90) as response:
        data = response.read()
    if sha256(data).hexdigest() != digest:
        raise ValueError(f"Downloaded file differs from the studio example: {path.name}")
    path.parent.mkdir(parents=True, exist_ok=True)
    with NamedTemporaryFile(dir=path.parent, delete=False) as output:
        output.write(data)
    Path(output.name).replace(path)


if __name__ == "__main__":
    for source, name, expected in FILES:
        fetch(source, ASSETS / name, expected)
        print(name)
