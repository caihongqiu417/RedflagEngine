"""Read Vulkan results in linear RGB and make the Stanford comparison sheets."""
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont
import yaml

ROOT = Path(__file__).resolve().parents[1]
RAW = ROOT / "tmp/stanford-effects"
OUT = ROOT / "output/scenes/stanford-vulkan"
SCENES = ("StanfordBunny", "StanfordDragon", "HappyBuddha")


def display(rgb):
    exposed = np.maximum(rgb, 0) * 14
    mapped = exposed / (1 + exposed)
    srgb = np.where(mapped < .0031308, 12.92 * mapped, 1.055 * mapped ** (1/2.4) - .055)
    return Image.fromarray(np.uint8(np.clip(srgb, 0, 1) * 255 + .5))


def compare(name):
    folder = RAW / name
    dimensions, color, settings = (folder / "scene.txt").read_text().splitlines()
    width, height = map(int, dimensions.split())
    color = np.fromstring(color, sep=" ")
    radius, roughness = map(float, settings.split())

    def read(stem, channels=4):
        values = np.fromfile(folder / (stem + ".f32"), dtype="<f4").reshape(height, width, channels)
        if not np.isfinite(values).all():
            raise ValueError(f"{name}/{stem} contains nonfinite pixels")
        return values

    surfaces = read("surfaces", 12)
    visible = surfaces[..., 3] != 0
    subject = visible & np.all(np.isclose(surfaces[..., 8:11], color, atol=1e-5), axis=2)
    if not 100 < subject.sum() < visible.sum() < width * height:
        raise ValueError(f"{name} needs a model, a floor and a background")

    def mse(a, b):
        return float(np.mean((a[visible, :3].astype(float) - b[visible, :3])**2))

    direct, reference = read("direct"), read("indirect-reference")
    ddgi, di, gi = read("ddgi"), read("restir-di"), read("restir-gi")
    ao, ssr, cluster, taa = (read(stem) for stem in ("ao", "ssr", "cluster", "taa"))
    samples = [read(f"sample-{frame}") for frame in range(8)]
    average = np.mean(samples, axis=0)

    scene = ROOT / f"RedflagForge/assets/scenes/Reference-{name}.redflag"
    entities = yaml.safe_load(scene.read_text(encoding="utf-8"))["Entities"]
    lamp = next(e for e in entities if "PointLightComponent" in e)
    light = lamp["PointLightComponent"]
    delta = lamp["TransformComponent"]["Translation"] - surfaces[..., :3]
    distance = np.maximum(np.linalg.norm(delta, axis=2), .0001)
    fade = np.maximum(0., 1 - distance**2 / radius**2)
    cos_theta = np.maximum(0., np.sum(surfaces[..., 4:7] * delta / distance[..., None], axis=2))
    intensity = np.array(light["Color"]) * light["Intensity"]
    expected = surfaces[..., 8:11] * intensity * (fade**2 * cos_theta / (np.pi * distance**2))[..., None]
    cluster_error = float(np.max(np.abs(cluster[visible, :3] - expected[visible])))
    assert cluster_error < 2e-5 and np.all((ao[visible, 3] >= 0) & (ao[visible, 3] <= 1))
    assert np.any(ssr[visible, 3] > 0) and np.any(taa[visible, 3] > 0)

    info = dict(width=width, height=height, visible_pixels=int(visible.sum()), model_pixels=int(subject.sum()),
                material_roughness=roughness, point_light_range=radius,
                ddgi_mse=mse(ddgi, reference), restir_gi_mse=mse(gi, reference), restir_di_mse=mse(di, direct),
                di_reused=float(np.mean(di[visible, 3] > 0)), gi_reused=float(np.mean(gi[visible, 3] > 0)),
                gtao_mean_visibility=float(ao[visible, 3].mean()), sssr_hit_fraction=float((ssr[visible, 3] > 0).mean()),
                cluster_max_rgb_error=cluster_error, cluster_mean_candidates=float(cluster[visible, 3].mean()),
                single_frame_mse=mse(samples[-1], average), taa_mse=mse(taa, average),
                taa_reused_fraction=float(np.mean(taa[visible, 3] > 0)))
    font = ImageFont.truetype("arial.ttf", 17)
    title = ImageFont.truetype("arialbd.ttf", 24)
    gap, margin, caption, header = 10, 16, 31, 46
    sheet = Image.new("RGB", (4*width+3*gap+2*margin, 2*(height+caption)+gap+header+margin), "#f7f7f3")
    draw = ImageDraw.Draw(sheet)
    draw.text((margin, 8), f"{name}    |    Vulkan    |    {width} x {height}", font=title, fill="#233733")
    for i, (label, rgb) in enumerate((
        ("Direct reference", direct[..., :3]), ("Direct + DDGI", direct[..., :3] + ddgi[..., :3]),
        ("ReSTIR DI", di[..., :3]), ("Direct + ReSTIR GI", direct[..., :3] + gi[..., :3]),
        ("GTAO", ao[..., :3]), ("Clustered lights", cluster[..., :3]),
        ("Stochastic reflections", ssr[..., :3]), ("TAA, frame 8", taa[..., :3]),
    )):
        x = margin + (i % 4) * (width + gap)
        y = header + (i // 4) * (height + caption + gap)
        sheet.paste(display(rgb), (x, y))
        draw.text((x, y + height + 4), label, font=font, fill="#243d38")
    sheet.save(OUT / (name + ".png"))
    return info


if __name__ == "__main__":
    OUT.mkdir(parents=True, exist_ok=True)
    report = dict(device=(RAW / "device.txt").read_text().strip(), seed=137, display_exposure=14,
                  shader_color_space="linear", indirect_reference_samples=128, frames=8,
                  scenes={name: compare(name) for name in SCENES})
    (OUT / "metrics.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
