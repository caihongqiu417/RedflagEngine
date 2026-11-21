"""Download the external glTF and images used by the Sponza rendering example."""
from concurrent.futures import ThreadPoolExecutor
import json
from http.client import IncompleteRead
from time import sleep
from urllib.error import URLError
from pathlib import Path
from urllib.request import urlopen

REVISION = "e124e8d8d7330a5372bc2b4d00d39a4a9d7997ea"
SOURCE = f"https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/{REVISION}/Models/Sponza/"
TARGET = Path(__file__).resolve().parents[1] / "RedflagForge/assets/models/reference/Sponza"


def download(name):
    target = TARGET / Path(name).name
    if target.exists():
        return target
    for attempt in range(4):
        try:
            with urlopen(SOURCE + name, timeout=120) as reply:
                data = reply.read()
            break
        except (IncompleteRead, URLError, TimeoutError):
            if attempt == 3:
                raise
            sleep(2 ** attempt)
    temporary = target.with_suffix(target.suffix + ".part")
    temporary.write_bytes(data)
    temporary.replace(target)
    return target


if __name__ == "__main__":
    TARGET.mkdir(parents=True, exist_ok=True)
    download("LICENSE.md")
    scene = download("glTF/Sponza.gltf")
    model = json.loads(scene.read_bytes())
    files = {entry["uri"] for entry in model["buffers"] + model["images"]}
    with ThreadPoolExecutor(max_workers=6) as workers:
        list(workers.map(download, ["glTF/" + name for name in sorted(files)]))
    print(f"{scene}: {len(files)} dependencies")
