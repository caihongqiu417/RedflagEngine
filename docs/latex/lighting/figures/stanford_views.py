"""Export two teaching plates from the Stanford Vulkan test's floating-point output."""
from pathlib import Path
from runpy import run_path

import numpy as np
from PIL import Image

figures = Path(__file__).resolve().parent
root = figures.parents[3]
display = run_path(str(root / "tests/stanford_effects_report.py"))["display"]


def read(scene, stem, channels=4):
    folder = root / "tmp/stanford-effects" / scene
    width, height = map(int, (folder / "scene.txt").read_text().splitlines()[0].split())
    return np.fromfile(folder / (stem + ".f32"), dtype="<f4").reshape(height, width, channels)


if __name__ == "__main__":
    direct = read("StanfordBunny", "direct")[..., :3]
    indirect = read("StanfordBunny", "ddgi")[..., :3]
    for name, rgb in (("direct", direct), ("bounce", indirect), ("combined", direct + indirect)):
        display(rgb).save(figures / f"bunny-{name}.png")

    display(read("StanfordDragon", "direct")[..., :3]).save(figures / "dragon-direct.png")
    visibility = read("StanfordDragon", "ao")[..., 3]
    gray = np.uint8(np.clip(visibility, 0, 1) * 255 + .5)
    rgb = np.repeat(gray[..., None], 3, axis=2)
    rgb[read("StanfordDragon", "surfaces", 12)[..., 3] == 0] = (130, 145, 156)
    Image.fromarray(rgb).save(figures / "dragon-visibility.png")
