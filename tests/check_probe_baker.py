"""用灯光工作室的副本核对 Vulkan 命令行、编辑器快捷键和失败时保留的烘焙文件。"""
import argparse
from copy import deepcopy
import hashlib
import json
import math
import msvcrt
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import time

import win32con as wc
import win32event
import win32gui
import win32process
import win32service
import yaml


ROOT = Path(__file__).resolve().parents[1]
FORGE = ROOT / "RedflagForge"
BIN = ROOT / "bin/Debug-windows-x86_64/RedflagForge"
# 编辑器正常启动时打开这个相对路径。测试目录里的同名文件保存的是灯光工作室副本。
ENTRY = Path("assets/scenes/Sponza-Atrium.redflag")


def dump_yaml(value):
    return yaml.safe_dump(value, allow_unicode=True, sort_keys=False).encode("utf-8")


def sha(data):
    return hashlib.sha256(data).hexdigest()


def probe_colors(data):
    magic, _, _ = struct.unpack_from("<8sQQ", data)
    if magic != b"RFDDGI1\0" or len(data) <= 24:
        raise AssertionError("探针文件缺少完整的文件头或光照数据")
    # 每条记录前 3 个 float 是 RGB。最后四项保存射线距离，比较光照时不计入。
    colors = [rgb for row in struct.iter_unpack("<8f", data[24:]) for rgb in row[:3]]
    if not all(map(math.isfinite, colors)) or max(colors) <= 0:
        raise AssertionError("探针颜色须为有限数，且应能测到灯照亮的方向")
    return colors


def gui_bake(exe, work, env, target, before_shortcut=None, error_message=None):
    """在独立 Windows 桌面向自己的窗口发送 Ctrl+Shift+B，避免影响正在使用的编辑器。"""
    name = f"RedflagProbeCheck{os.getpid()}"
    desktop = win32service.CreateDesktop(name, 0, wc.GENERIC_ALL, None)
    start = win32process.STARTUPINFO()
    start.lpDesktop = "WinSta0\\" + name
    start.dwFlags = wc.STARTF_USESTDHANDLES
    log = work / "editor-hotkey.log"
    try:
        with log.open("wb") as output, open(os.devnull, "rb") as stdin:
            start.hStdOutput = start.hStdError = msvcrt.get_osfhandle(output.fileno())
            start.hStdInput = msvcrt.get_osfhandle(stdin.fileno())
            for handle in (start.hStdOutput, start.hStdInput):
                os.set_handle_inheritable(int(handle), True)
            cmd = subprocess.list2cmdline([str(exe), "--vulkan"])
            proc, thread, pid, _ = win32process.CreateProcess(
                None, cmd, None, None, True, wc.CREATE_NO_WINDOW, env, str(work), start)
            thread.Close()
            try:
                def wait_for(predicate):
                    deadline = time.monotonic() + 90
                    while win32event.WaitForSingleObject(proc, 100) == wc.WAIT_TIMEOUT:
                        result = predicate()
                        if result:
                            return result
                        if time.monotonic() > deadline:
                            raise TimeoutError(str(log))
                    raise RuntimeError(f"编辑器提前退出，请查看 {log}")

                def editor_window():
                    if b"Lighting preview " not in log.read_bytes():
                        return None
                    return next((int(h) for h in desktop.EnumDesktopWindows()
                                 if win32process.GetWindowThreadProcessId(int(h))[1] == pid and
                                 win32gui.GetWindowText(int(h)) == "Redflag Forge"), None)

                hwnd = wait_for(editor_window)
                # 等待首次布局完成，再在“质量与参数”面板触发正常烘焙操作。
                time.sleep(1)
                if before_shortcut:
                    before_shortcut()
                keys = [(wc.VK_CONTROL, 0x1d), (wc.VK_SHIFT, 0x2a), (ord("B"), 0x30)]
                win32gui.PostMessage(hwnd, wc.WM_SETFOCUS, 0, 0)
                for key, scan in keys:
                    win32gui.PostMessage(hwnd, wc.WM_KEYDOWN, key, 1 | (scan << 16))
                time.sleep(.4)
                for key, scan in reversed(keys):
                    win32gui.PostMessage(hwnd, wc.WM_KEYUP, key, 1 | (scan << 16) | (3 << 30))
                if error_message:
                    wait_for(lambda: error_message in log.read_bytes())
                else:
                    wait_for(target.is_file)
                win32gui.PostMessage(hwnd, wc.WM_CLOSE, 0, 0)
                if win32event.WaitForSingleObject(proc, 15000) != wc.WAIT_OBJECT_0:
                    raise TimeoutError("编辑器退出超时")
                return win32process.GetExitCodeProcess(proc)
            finally:
                if win32event.WaitForSingleObject(proc, 0) == wc.WAIT_TIMEOUT:
                    win32process.TerminateProcess(proc, 1)
                    win32event.WaitForSingleObject(proc, 5000)
                proc.Close()
    finally:
        desktop.CloseDesktop()


def run(exe, work):
    for directory in ("mono", "Resources", "assets/models/LightPanel", "assets/fonts", "assets/icons"):
        shutil.copytree(FORGE / directory, work / directory)
    entry = work / ENTRY
    entry.parent.mkdir(parents=True)
    settings = entry.with_suffix(".redflag.render.yaml")
    probes = entry.with_suffix(".redflag.rfprobes")
    original = FORGE / "assets/scenes/Light-Studio.redflag"
    original_settings = original.with_suffix(".redflag.render.yaml")
    sources = (original.read_bytes(), original_settings.read_bytes())
    scene = yaml.safe_load(sources[0])
    scene["Entities"].append({
        "Entity": 53, "TagComponent": {"Tag": "测试点光源"},
        "TransformComponent": {"Translation": [0, 1.5, 1.5], "Rotation": [0, 0, 0], "Scale": [1, 1, 1]},
        "PointLightComponent": {"Color": [1, .8, .6], "Intensity": 4, "Radius": 8, "CastShadow": False},
    })
    defaults = yaml.safe_load(sources[1])
    defaults.update(DDGIProbeFeedback=True, PreviewWidth=160, TAA=False)
    env = dict(os.environ, PATH=str(BIN) + os.pathsep + os.environ["PATH"], RF_VULKAN_VALIDATION="1")
    results = {}

    def bake(label, scene_bytes, options, *, gui=False, fails=False):
        entry.write_bytes(scene_bytes)
        settings_bytes = dump_yaml(options)
        settings.write_bytes(settings_bytes)
        previous = probes.read_bytes() if fails else None
        if gui:
            code = gui_bake(exe, work, env, probes)
        else:
            with (work / (label + ".log")).open("wb") as log:
                cmd = [exe, "--bake-probes", ENTRY]
                code = subprocess.run(cmd, cwd=work, env=env, stdout=log, stderr=subprocess.STDOUT,
                                      timeout=120, creationflags=subprocess.CREATE_NO_WINDOW).returncode
        if (code != 0) != fails:
            raise AssertionError(f"{label}: 意外的退出码 {code}，见 {work}")
        if entry.read_bytes() != scene_bytes or settings.read_bytes() != settings_bytes:
            raise AssertionError(f"{label}: 烘焙改动了场景或渲染设置")
        data = probes.read_bytes()
        if fails and data != previous:
            raise AssertionError(f"{label}: 失败后覆盖了先前成功的文件")
        results[label] = dict(exit_code=code, scene_and_settings_unchanged=True,
                              probe_sha256=sha(data), payload_sha256=sha(data[24:]))
        if fails:
            results[label]["previous_bake_preserved"] = True
        print(label, code, sha(data)[:16], flush=True)
        return data

    baseline = dump_yaml(scene)
    reference = bake("baseline", baseline, defaults)
    rgb = probe_colors(reference)
    probes.unlink()  # 强制从头生成文件，不能靠原来的文件通过比较。
    if bake("new-process", baseline, defaults) != reference:
        raise AssertionError("第二个进程生成的文件与首次烘焙不一致")

    # 两个进程分别读“红灯、蓝灯”和“蓝灯、红灯”。两盏灯本身没动，固定采样种子后每个探针都应得到相同的 RGB。
    pair = deepcopy(scene)
    red = pair["Entities"][-1]
    red["PointLightComponent"]["Color"] = [1, 0, 0]
    blue = deepcopy(red)
    blue.update(Entity=54, TagComponent={"Tag": "蓝灯"})
    blue["PointLightComponent"]["Color"] = [0, 0, 1]
    pair["Entities"].append(blue)
    first = bake("red-blue", dump_yaml(pair), defaults)
    pair["Entities"][-2:] = [blue, red]
    second = bake("blue-red", dump_yaml(pair), defaults)
    if first != second:
        (work / "red-blue.rfprobes").write_bytes(first)
        (work / "blue-red.rfprobes").write_bytes(second)
        raise AssertionError("只交换两盏灯的保存顺序，重新烘焙的文件却变了")

    dark_wall = deepcopy(scene)
    wall = next(entity for entity in dark_wall["Entities"] if entity["TagComponent"]["Tag"] == "白墙")
    wall["MeshRendererComponent"]["BaseColor"] = [.05, .05, .05, 1]
    bright_light = deepcopy(scene)
    bright_light["Entities"][-1]["PointLightComponent"]["Intensity"] = 32
    for label, scene_bytes, options in (
            ("single-bounce", baseline, dict(defaults, DDGIProbeFeedback=False)),
            ("dark-wall", dump_yaml(dark_wall), defaults),
            ("bright-point-light", dump_yaml(bright_light), defaults)):
        data = bake(label, scene_bytes, options)
        values = probe_colors(data)
        if len(values) != len(rgb):
            raise AssertionError(f"{label}: 同一间房间应有相同数量的探针记录")
        changed = sum(a != b for a, b in zip(values, rgb))
        if not changed:
            raise AssertionError(f"{label}: 探针收到的 RGB 亮度没有变化")
        results[label]["changed_rgb_channels_vs_baseline"] = changed

    missing = deepcopy(scene)
    model = next(e["ModelRendererComponent"] for e in missing["Entities"] if "ModelRendererComponent" in e)
    model.update(ModelID=0, FilePath="assets/models/absent.gltf")
    bake("missing-model", dump_yaml(missing), defaults, fails=True)
    bake("damaged-scene", b"Scene: damaged\nEntities: [\n", defaults, fails=True)
    probes.unlink()
    # 灯牌原来绕 Y 轴旋转 1.28 弧度。仅显示属性曾使它在度和弧度之间换算时改变一位。
    gui_result = bake("editor-hotkey", baseline, defaults, gui=True)
    if gui_result != reference:
        (work / "baseline.rfprobes").write_bytes(reference)
        raise AssertionError(f"编辑器只打开属性就改变了烘焙结果，比较 {probes} 与 {work / 'baseline.rfprobes'}")
    if sources != (original.read_bytes(), original_settings.read_bytes()):
        raise AssertionError("项目里的原场景或渲染设置发生了改变")
    return dict(scene=original.name, extra_point_light_intensity=4, exe_sha256=sha(exe.read_bytes()),
                probe_file_bytes=len(reference), probe_rgb_channels=len(rgb), results=results)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=BIN / "RedflagForge.exe")
    parser.add_argument("--report", type=Path, help="另存通过后的 JSON 报告")
    args = parser.parse_args()
    exe = args.exe.resolve(strict=True)
    base = ROOT / "tmp" / "probe-baker"
    base.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(dir=base))
    print(f"测试副本与日志：{work}", flush=True)
    result = run(exe, work)
    report = json.dumps(result, indent=2) + "\n"
    (work / "report.json").write_text(report, encoding="utf-8")
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(report, encoding="utf-8")


if __name__ == "__main__":
    main()
