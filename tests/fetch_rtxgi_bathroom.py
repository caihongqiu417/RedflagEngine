"""Download cenobi's Bathroom from NVIDIA's RTXGI sample-asset repository."""
from hashlib import sha256
from pathlib import Path
from tempfile import NamedTemporaryFile
from urllib.request import urlopen

REVISION = "6c3caa658d419a00866fcf6b387b0140c1950e17"
SOURCE = f"https://raw.githubusercontent.com/NVIDIA-RTX/RTXGI-Assets/{REVISION}/Bathroom/"
TARGET = Path(__file__).resolve().parents[1] / "RedflagForge/assets/models/reference/RTXGI-Bathroom"
FILES = {
    "BLENDSWAP_LICENSE.txt": "350ca973f1b6eeb20cf67c7d4e62d0063609c75cbd69bdf45b0d76e5078204f4",
    "LAZIENKA.glb": "136b2b89888bb306cc2a31e60db4a472a4ee950177640a16ad462123fb35f45a",
}


def fetch(name, digest):
    target = TARGET / name
    if target.is_file() and sha256(target.read_bytes()).hexdigest() == digest:
        return
    with urlopen(SOURCE + name, timeout=90) as response:
        data = response.read()
    if sha256(data).hexdigest() != digest:
        raise ValueError(f"Downloaded file differs from the pinned revision: {name}")
    with NamedTemporaryFile(dir=TARGET, delete=False) as temporary:
        temporary.write(data)
    Path(temporary.name).replace(target)


if __name__ == "__main__":
    TARGET.mkdir(parents=True, exist_ok=True)
    for name, digest in FILES.items():
        fetch(name, digest)
    print(f"Verified {TARGET / 'LAZIENKA.glb'}")
