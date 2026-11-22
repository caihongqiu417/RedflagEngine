"""让两个 Vulkan 驱动各烘焙一次，再用编辑器读取另一个驱动生成的文件。"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import tempfile

import numpy as np
import yaml

from check_directional_bake import bake, inspect
from check_probe_baker import BIN, ENTRY, FORGE, ROOT
from check_probe_save_as import save
from profile_native_navigation import prepare


def run(exe, second_driver):
    scene = FORGE / "assets/scenes/Light-Studio.redflag"
    settings = yaml.safe_load(Path(str(scene) + ".render.yaml").read_bytes())
    settings.update(Shading="Base color", PreviewWidth=128)
    config = yaml.safe_dump(settings).encode("utf8")
    env = dict(os.environ, PATH=str(BIN) + os.pathsep + os.environ["PATH"], RF_VULKAN_VALIDATION="1")
    environments = [env, dict(env, VK_DRIVER_FILES=str(second_driver))]
    work = Path(tempfile.mkdtemp(prefix="probe-drivers-", dir=ROOT / "tmp"))
    prepare(work)
    bakes = [bake(exe, work, active, f"bake-{i}", scene.read_bytes(), config) for i, active in enumerate(environments)]
    assert bakes[0][1] != bakes[1][1], "请选择与系统默认驱动不同的 Vulkan 实现"
    colors, stats = zip(*(inspect(data) for data, _ in bakes))
    assert stats[0]["scene_key"] == stats[1]["scene_key"]
    for i, (data, device) in enumerate(bakes):
        # 给正式编辑器一份来自另一个驱动的烘焙。另存为会先校验场景，成功后才写出目标文件。
        probes = Path(str(work / ENTRY) + ".rfprobes")
        probes.write_bytes(data)
        destination = work / f"reopened-{i}.redflag"
        log = save(exe, work, environments[1-i], destination)
        assert bakes[1-i][1] in log and "VUID-" not in log and "Validation Error" not in log
        assert Path(str(destination) + ".rfprobes").read_bytes() == data
        stats[i].update(baked_on=device, opened_on=bakes[1-i][1], preserved_exactly=True)
    diff = np.abs(colors[0] - colors[1])
    with exe.open("rb") as binary:
        executable_sha256 = hashlib.file_digest(binary, "sha256").hexdigest()
    return dict(scene=scene.name, executable_sha256=executable_sha256, drivers=stats,
                max_rebake_rgb_difference=float(diff.max()), mean_rebake_rgb_difference=float(diff.mean()))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("second_driver", type=Path, help="Vulkan ICD 的 JSON 文件，例如 SwiftShader 随附的清单")
    parser.add_argument("--exe", type=Path, default=BIN / "RedflagForge.exe")
    args = parser.parse_args()
    result = run(args.exe.resolve(strict=True), args.second_driver.resolve(strict=True))
    out = ROOT / "output/validation/probe-drivers.json"
    out.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf8")
    print(out)
