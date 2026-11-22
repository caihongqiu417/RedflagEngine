"""在独立项目里移动灯牌，再移走它的发光图，核对烘焙按钮和命令行怎样处理上一次的文件。"""
import argparse
import base64
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

import yaml

from check_probe_baker import BIN, ENTRY, FORGE, ROOT, dump_yaml, gui_bake, sha
from check_probe_save_as import save


def run(exe, before, work):
    env = dict(os.environ, PATH=str(BIN) + os.pathsep + os.environ["PATH"], RF_VULKAN_VALIDATION="1")
    for directory in ("mono", "Resources", "assets/fonts", "assets/icons"):
        shutil.copytree(FORGE / directory, work / directory)

    original = FORGE / "assets/scenes/Light-Studio.redflag"
    entry = work / ENTRY
    entry.parent.mkdir()
    entry.write_bytes(original.read_bytes())
    settings = yaml.safe_load(original.with_suffix(".redflag.render.yaml").read_bytes())
    settings.update(PreviewWidth=160, TAA=False)
    entry.with_suffix(".redflag.render.yaml").write_bytes(dump_yaml(settings))
    probes = entry.with_suffix(".redflag.rfprobes")

    # 场景仍存旧路径。模型与 .rfmeta 一起改名后，AssetLibrary 应按场景里的 ID 找到它。
    source_model = Path("assets/models/LightPanel/LightPanel.gltf")
    old_model = work / source_model
    model = work / "assets/models/props/Lamp.gltf"
    old_model.parent.mkdir(parents=True)
    model.parent.mkdir(parents=True)
    for suffix in ("", ".rfmeta"):
        shutil.copyfile(str(FORGE / source_model) + suffix, str(old_model) + suffix)

    def bake(label, expected_success=True, program=exe):
        proc = subprocess.run([str(program), "--bake-probes", str(ENTRY)], cwd=work, env=env, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, timeout=120, creationflags=subprocess.CREATE_NO_WINDOW)
        (work / (label + ".log")).write_bytes(proc.stdout)
        assert (proc.returncode == 0) == expected_success and b"VUID-" not in proc.stdout, label
        if not expected_success:
            assert b"emitter.png" in proc.stdout, label
        return proc.returncode

    bake("original-location")
    reference = probes.read_bytes()
    shipped = original.with_suffix(".redflag.rfprobes").read_bytes()
    assert reference[8:16] == shipped[8:16], "相同场景的探针校验值应一致"
    for suffix in ("", ".rfmeta"):
        Path(str(old_model) + suffix).rename(str(model) + suffix)
    # 让新开的编辑器读取随仓烘焙。另存为只有在本次场景的光照校验通过后才会复制它。
    probes.write_bytes(shipped)
    saved = work / "relocated.redflag"
    assert "Saved scene and rendering settings" in save(exe, work, env, saved)
    assert saved.with_suffix(".redflag.rfprobes").read_bytes() == shipped
    bake("moved-model")
    assert probes.read_bytes() == reference
    gltf = json.loads(model.read_bytes())
    image = model.with_name("emitter.png")
    png = base64.b64decode(gltf["images"][0]["uri"].split(",", 1)[1])
    gltf["images"][0]["uri"] = image.name
    model.write_text(json.dumps(gltf), encoding="utf-8")
    image.write_bytes(png)
    bake("external-image")
    assert probes.read_bytes() == reference, "相同的 PNG 改为外部文件后，光照应相同"

    log = (work / "moved-model.log").read_text(encoding="utf-8", errors="replace")
    results = dict(executable_sha256=sha(exe.read_bytes()), scene=original.name,
                   vulkan_device=re.search(r"Vulkan device: (.+)", log).group(1).strip(),
                   matches_shipped_file_on_this_device=reference == shipped,
                   editor_loaded_shipped_file_after_move=True, moved_model_and_sidecar_matched=True,
                   embedded_and_external_matched=True,
                   probe_sha256=sha(reference), texture_sha256=sha(png), errors={})
    for label in ("missing", "damaged"):
        if label == "missing":
            image.unlink()
        else:
            image.write_bytes(b"This is not a PNG.")
        modified = probes.stat().st_mtime_ns
        code = bake(label, False)
        assert probes.read_bytes() == reference and probes.stat().st_mtime_ns == modified, label
        results["errors"][label] = dict(exit_code=code, previous_bake_preserved=True)

    # 编辑器已读到正常图片才移走文件。快捷键触发与按钮相同的烘焙操作。
    image.write_bytes(png)
    modified = probes.stat().st_mtime_ns
    code = gui_bake(exe, work, env, probes, before_shortcut=image.unlink,
                    error_message="烘焙需要的贴图无法读取".encode("utf-8"))
    image_log = (work / "editor-hotkey.log").rename(work / "editor-missing-image.log")
    assert code == 0 and b"emitter.png" in image_log.read_bytes()
    assert probes.read_bytes() == reference and probes.stat().st_mtime_ns == modified
    results["errors"]["removed_while_editor_open"] = dict(editor_exit_code=code, previous_bake_preserved=True)
    image.write_bytes(png)
    source_bytes = model.read_bytes()
    code = gui_bake(exe, work, env, probes, before_shortcut=model.unlink, error_message=b"Probe bake failed:")
    model_log = (work / "editor-hotkey.log").rename(work / "editor-missing-model.log")
    assert code == 0 and b"Lamp.gltf" in model_log.read_bytes()
    assert probes.read_bytes() == reference and probes.stat().st_mtime_ns == modified
    results["errors"]["model_removed_while_editor_open"] = dict(editor_exit_code=code, previous_bake_preserved=True)
    model.write_bytes(source_bytes)
    bake("restored")
    assert probes.read_bytes() == reference
    results["restored_image_matched"] = True

    if before:
        bake("before-present", program=before)
        valid = probes.read_bytes()
        image.unlink()
        code = bake("before-missing", program=before)
        overwritten = probes.read_bytes()
        assert valid != overwritten
        results["before"] = dict(executable_sha256=sha(before.read_bytes()), exit_code_without_texture=code,
                                 with_texture_sha256=sha(valid), without_texture_sha256=sha(overwritten))
    return results


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=BIN / "RedflagForge.exe")
    parser.add_argument("--before", type=Path, help="另用旧程序重现缺图时覆盖原烘焙的问题")
    parser.add_argument("--report", type=Path, help="另存通过后的 JSON")
    args = parser.parse_args()
    (ROOT / "tmp").mkdir(exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="probe-textures-", dir=ROOT / "tmp"))
    print(f"测试场景和日志 {work}", flush=True)
    result = run(args.exe.resolve(strict=True), args.before.resolve(strict=True) if args.before else None, work)
    report = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
    (work / "report.json").write_text(report, encoding="utf-8")
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(report, encoding="utf-8")
    print(report)
