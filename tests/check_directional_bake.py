"""用正式 Vulkan 编辑器烘焙 Stanford Bunny，比较转动方向光前后探针收到的光。"""
import argparse
from copy import deepcopy
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile

import numpy as np
import yaml

from check_probe_baker import BIN, ENTRY, FORGE, ROOT, dump_yaml, sha
from check_probe_save_as import save
from profile_native_navigation import prepare


SCENES = FORGE / "assets/scenes"
BUNNY = SCENES / "Reference-StanfordBunny.redflag"


def with_sun(scene, **changes):
    result = deepcopy(scene)
    light = next(e["DirectionalLightComponent"] for e in result["Entities"] if "DirectionalLightComponent" in e)
    light.update(changes)
    return result


def bake(exe, work, env, label, scene, settings):
    entry = work / ENTRY
    entry.write_bytes(scene)
    entry.with_suffix(".redflag.render.yaml").write_bytes(settings)
    probes = entry.with_suffix(".redflag.rfprobes")
    probes.unlink(missing_ok=True)
    run = subprocess.run([str(exe), "--bake-probes", str(ENTRY)], cwd=work, env=env,
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120,
                         creationflags=subprocess.CREATE_NO_WINDOW)
    (work / (label + ".log")).write_bytes(run.stdout)
    log = run.stdout.decode("utf-8", errors="replace")
    if run.returncode or "VUID-" in log or "Validation Error" in log:
        raise RuntimeError(f"{label} 烘焙或 Vulkan 验证失败，见 {work}")
    if scene != entry.read_bytes() or settings != entry.with_suffix(".redflag.render.yaml").read_bytes():
        raise AssertionError(f"{label} 烘焙改写了输入文件")
    return probes.read_bytes(), re.search(r"Vulkan device: (.+)", log).group(1).strip()


def inspect(data):
    magic, key, _ = struct.unpack_from("<8sQQ", data)
    assert magic == b"RFDDGI1\0"
    # 每个探针方向有两个 vec4。前三个 float 是 RGB，射线距离单独保存。
    rgb = np.frombuffer(data, dtype="<f4", offset=24).reshape(-1, 8)[:, :3]
    assert rgb.size and np.all(np.isfinite(rgb)) and np.all(rgb >= 0)
    return rgb, dict(scene_key=f"{key:016x}", sha256=sha(data), probe_faces=len(rgb),
                     lit_faces=int(np.count_nonzero(rgb.any(axis=1))), mean_rgb=rgb.mean(axis=0, dtype=np.float64).tolist(),
                     max_rgb=rgb.max(axis=0).tolist())


def run(exe, before, work):
    prepare(work)
    env = dict(os.environ, PATH=str(BIN) + os.pathsep + os.environ["PATH"], RF_VULKAN_VALIDATION="1")
    # 兔子和地面的几何、材质与原场景一致。移除点光源后，方向光是唯一的光源。
    scene = yaml.safe_load(BUNNY.read_bytes())
    scene["Entities"] = [e for e in scene["Entities"] if "PointLightComponent" not in e]
    light = next(e["DirectionalLightComponent"] for e in scene["Entities"] if "DirectionalLightComponent" in e)
    unlit = dict(scene, Entities=[e for e in scene["Entities"] if "DirectionalLightComponent" not in e])
    turned_direction = [1, -.18, 0]
    scenes = dict(off=unlit, white=scene, double=with_sun(scene, Intensity=2 * light["Intensity"]),
                  red=with_sun(scene, Color=[1, 0, 0]), side=with_sun(scene, Direction=turned_direction),
                  no_shadows=with_sun(scene, CastShadow=False))
    # 只算第一处表面反射的光，容易核对输入光强与探针亮度的比例。
    settings = dump_yaml(dict(Shading="Base color", DDGIProbeFeedback=False, PreviewWidth=160))
    report = dict(scene=BUNNY.relative_to(ROOT).as_posix(), directional_light=light, feedback=False,
                  executable_sha256=sha(exe.read_bytes()), cases={})
    files, colors = {}, {}
    for name, modified in scenes.items():
        files[name], report["vulkan_device"] = bake(exe, work, env, name, dump_yaml(modified), settings)
        colors[name], report["cases"][name] = inspect(files[name])

    assert not colors["off"].any() and colors["white"].any(), "方向光应照亮兔子或地面，无灯时探针应为黑色"
    np.testing.assert_allclose(colors["double"], 2 * colors["white"], rtol=2e-5, atol=1e-6)
    np.testing.assert_allclose(colors["red"][:, 0], colors["white"][:, 0], rtol=2e-5, atol=1e-6)
    assert not colors["red"][:, 1:].any(), "纯红色方向光不应产生绿色或蓝色反射"
    changed_faces = np.any(abs(colors["side"] - colors["white"]) > 1e-6, axis=1)
    assert changed_faces.any(), "转动方向光后，探针收到的光应当改变"
    assert len({row["scene_key"] for row in report["cases"].values()}) == len(scenes)
    shadow_loss = colors["no_shadows"] - colors["white"]
    assert shadow_loss.min() >= -1e-6 and shadow_loss.max() > 1e-6, "开启遮挡应减少光照"
    report["faces_dimmed_by_shadows"] = int(np.any(shadow_loss > 1e-6, axis=1).sum())
    report["side_direction"] = turned_direction
    report["changed_faces_after_turn"] = int(changed_faces.sum())
    report["max_rgb_error_at_double_intensity"] = float(abs(colors["double"] - 2 * colors["white"]).max())

    old = None
    if before:
        report["before_executable_sha256"] = sha(before.read_bytes())
        report["before"] = {}
        for name in ("off", "white"):
            old, _ = bake(before, work, env, "before-" + name, dump_yaml(scenes[name]), settings)
            _, report["before"][name] = inspect(old)
        assert report["before"]["white"]["lit_faces"] == 0 and report["before"]["off"] == report["before"]["white"]

    # 编辑器「另存为」只会在 LoadProbes 成功时才复制烘焙文件。新进程分别读入有效与过期的文件。
    reloads = [("same", scene, files["white"], True), ("turned", scenes["side"], files["white"], False)]
    if old is not None:
        reloads.append(("before-fix", scene, old, False))
    entry = work / ENTRY
    report["editor_copied_bake"] = {}
    for name, modified, candidate, accepted in reloads:
        entry.write_bytes(dump_yaml(modified))
        entry.with_suffix(".redflag.rfprobes").write_bytes(candidate)
        destination = work / (name + ".redflag")
        log = save(exe, work, env, destination)
        assert destination.is_file() and "Saved scene and rendering settings" in log, name
        copied = destination.with_suffix(".redflag.rfprobes")
        assert copied.exists() == accepted, name
        if accepted:
            assert copied.read_bytes() == candidate, name
        report["editor_copied_bake"][name] = accepted

    report["shipped_scenes"] = {}
    for name in ("Light-Studio", "Mirror-Studio", "RTXGI-Bathroom"):
        source = SCENES / (name + ".redflag")
        generated, _ = bake(exe, work, env, name, source.read_bytes(), source.with_suffix(".redflag.render.yaml").read_bytes())
        shipped = source.with_suffix(".redflag.rfprobes").read_bytes()
        _, expected = inspect(shipped)
        _, actual = inspect(generated)
        assert actual["scene_key"] == expected["scene_key"], f"{name} 随仓烘焙与当前程序的场景校验值不同"
        report["shipped_scenes"][name] = dict(scene_key=actual["scene_key"], shipped_sha256=expected["sha256"],
                                              generated_sha256=actual["sha256"], identical=generated == shipped)
        if before:
            previous, _ = bake(before, work, env, "before-" + name, source.read_bytes(),
                               source.with_suffix(".redflag.render.yaml").read_bytes())
            previous_rgb, prior = inspect(previous)
            current_rgb, _ = inspect(generated)
            report["shipped_scenes"][name].update(before_scene_key=prior["scene_key"], before_sha256=prior["sha256"],
                                                  before_payload_identical=previous[24:] == generated[24:],
                                                  max_rgb_difference=float(abs(previous_rgb - current_rgb).max()))
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=BIN / "RedflagForge.exe")
    parser.add_argument("--before", type=Path, help="另用修复前的可执行文件重现旧结果")
    parser.add_argument("--report", type=Path, help="通过后另存 JSON")
    args = parser.parse_args()
    (ROOT / "tmp").mkdir(exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="directional-bake-", dir=ROOT / "tmp"))
    print(f"场景副本与进程日志 {work}", flush=True)
    result = run(args.exe.resolve(strict=True), args.before.resolve(strict=True) if args.before else None, work)
    report = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
    (work / "report.json").write_text(report, encoding="utf-8")
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(report, encoding="utf-8")
    print(report)
