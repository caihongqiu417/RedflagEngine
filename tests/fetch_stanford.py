"""Fetch the three Stanford scans used by the Redflag scene gallery."""
from concurrent.futures import ThreadPoolExecutor
from hashlib import sha256
from io import BytesIO
from pathlib import Path
import tarfile
from urllib.request import urlopen

ROOT = Path(__file__).resolve().parents[1]
BASE = "https://graphics.stanford.edu/pub/3Dscanrep/"
MODELS = [
    ("StanfordBunny", "bunny.tar.gz", "bunny/reconstruction/bun_zipper_res2.ply",
     "a5720bd96d158df403d153381b8411a727a1d73cff2f33dc9b212d6f75455b84",
     "8faa052bb08cf1625eec97b6508baceee11eec9cf28f83f6a9d4547ab15d7761"),
    ("StanfordDragon", "dragon/dragon_recon.tar.gz", "dragon_recon/dragon_vrip_res4.ply",
     "74ac1d90989c9b1732edee82d57e9ce71452144cf4355f108d8c9c616d28d02f",
     "23fe8c7b3b006de91d5307504c4a1624e5e5e4d4fcb9e96569b00ce971fdc480"),
    ("HappyBuddha", "happy/happy_recon.tar.gz", "happy_recon/happy_vrip_res4.ply",
     "409cd294efbfd8244e15a382b95a9423f153b7776e736c9b09f19ec9d3c10ed0",
     "d8a420c1ea113c96a8551c4892dc7058833ad64fece1d81ac783865b38940b8e"),
]


def fetch(model):
    name, archive, member, archive_hash, ply_hash = model
    dest = ROOT / "RedflagForge/assets/models/reference" / name / Path(member).name
    if dest.exists() and sha256(dest.read_bytes()).hexdigest() == ply_hash:
        return dest

    cache = ROOT / "tmp/reference-input" / Path(archive).name
    if cache.exists() and sha256(cache.read_bytes()).hexdigest() == archive_hash:
        data = cache.read_bytes()
    else:
        with urlopen(BASE + archive, timeout=120) as response:
            data = response.read()
        if sha256(data).hexdigest() != archive_hash:
            raise ValueError(f"{archive}: archive checksum changed")
        cache.parent.mkdir(parents=True, exist_ok=True)
        cache.write_bytes(data)

    with tarfile.open(fileobj=BytesIO(data), mode="r:gz") as package:
        ply = package.extractfile(member).read()
    if sha256(ply).hexdigest() != ply_hash:
        raise ValueError(f"{member}: mesh checksum changed")
    dest.parent.mkdir(parents=True, exist_ok=True)
    temporary = dest.with_suffix(".part")
    temporary.write_bytes(ply)
    temporary.replace(dest)
    return dest


if __name__ == "__main__":
    with ThreadPoolExecutor(max_workers=3) as workers:
        for mesh in workers.map(fetch, MODELS):
            print(mesh)
    name, archive, _, archive_hash, _ = MODELS[0]
    for level, checksum in [(3, "d628625d14fbe5deecb123536b515405c522984d43488cc24f91980eb75edadc"),
                            (4, "2ca4b1c9206eb2768f10eeb06f30d2dcf576c21f69243335660c896605d1f0ab")]:
        print(fetch((name, archive, f"bunny/reconstruction/bun_zipper_res{level}.ply", archive_hash, checksum)))
