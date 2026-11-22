"""在正式 Vulkan 编辑器中拍摄 BVH 角色的按键控制或 PhysX 双腿角色的行走、落地。"""
import argparse
import ctypes
import hashlib
import json
import msvcrt
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time

import numpy as np
from PIL import Image
import pywintypes
import win32api
import win32con
import win32event
import win32gui
import win32process
import win32service

from profile_native_navigation import ROOT, prepare, save_view


def capture(exe, out, physics=None):
    work = Path(tempfile.mkdtemp(prefix="character-input-", dir=ROOT / "tmp"))
    prepare(work)
    out.mkdir(parents=True, exist_ok=True)
    samples = ROOT / "RedflagForge/assets/scenes"
    scene = samples / ("Character-Physics.redflag" if physics else "Character-Walk.redflag")
    if physics == "drop":
        # 保留正式相机和地台，只把骨盆抬高 1 米并关闭关节驱动。
        text = scene.read_text(encoding="utf8")
        assert text.count("[-1.2, 0.91, 0]") == text.count("Walking: true") == 1
        saved = work / "assets/scenes/Character-Physics.redflag"
        saved.write_text(text.replace("[-1.2, 0.91, 0]", "[-1.2, 1.91, 0]").replace("Walking: true", "Walking: false"), encoding="utf8")
        shutil.copyfile(str(scene)+".render.yaml", str(saved)+".render.yaml")
        scene = saved
    prefix = "physics-" + physics if physics else "control"
    log = work / "forge.log"
    env = dict(os.environ, RF_VULKAN_VALIDATION="1")
    name = "RedflagCharacter" + str(os.getpid())
    desktop = win32service.CreateDesktop(name, 0, win32con.GENERIC_ALL, None)
    startup = win32process.STARTUPINFO()
    startup.lpDesktop = "WinSta0\\" + name
    startup.dwFlags = win32con.STARTF_USESTDHANDLES

    def key(code, down):
        scan = win32api.MapVirtualKey(code, 0)
        flags = 1 | (scan << 16) | ((code == win32con.VK_LEFT) << 24)
        window_message = win32con.WM_KEYDOWN if down else win32con.WM_KEYUP
        win32gui.PostMessage(hwnd, window_message, code, flags if down else flags | (3 << 30))

    def toggle():
        key(win32con.VK_F5, True)
        time.sleep(.12)
        key(win32con.VK_F5, False)
        time.sleep(.55)

    def record(label):
        full = work / (label + ".png")
        save_view(hwnd, full)
        panel = Image.open(full).crop(viewport)
        panel.save(out / (prefix + "-" + label + ".png"))
        # 棕色像素属于角色。行走比较它们的平均横坐标，落地比较从图像顶部算起的纵坐标。
        rgb = np.asarray(panel.convert("RGB"), dtype=np.int16)
        y, x = np.where((rgb[:,:,0] > rgb[:,:,1]+8) & (rgb[:,:,1] > rgb[:,:,2]+8))
        assert len(x) > 100, "No visible character"
        axis = y if physics == "drop" else x
        return round(float(axis.mean()), 2)

    with log.open("wb") as stream, open(os.devnull, "rb") as inp:
        startup.hStdOutput = startup.hStdError = msvcrt.get_osfhandle(stream.fileno())
        startup.hStdInput = msvcrt.get_osfhandle(inp.fileno())
        for handle in (startup.hStdInput, startup.hStdOutput):
            os.set_handle_inheritable(handle, True)
        cmd = subprocess.list2cmdline([str(exe), "--vulkan", "--scene", str(scene)])
        proc, thread, pid, _ = win32process.CreateProcess(None, cmd, None, None, True, win32con.CREATE_NO_WINDOW,
                                                         env, str(work), startup)
        thread.Close()
        hwnd = None
        try:
            for _ in range(200):
                try:
                    hwnd = next((int(w) for w in desktop.EnumDesktopWindows()
                                 if win32process.GetWindowThreadProcessId(w)[1] == pid and
                                 win32gui.GetWindowText(w) == "Redflag Forge"), None)
                except pywintypes.error:
                    hwnd = None
                if hwnd and b"Lighting preview " in log.read_bytes(): break
                time.sleep(.1)
            assert hwnd
            ini = work / "imgui-vulkan.ini"
            for _ in range(100):
                if ini.is_file() and "[Window][场景视口]" in ini.read_text(encoding="utf8"): break
                time.sleep(.1)
            text = log.read_text(encoding="utf8")
            width, height = map(int, re.findall(r"viewport (\d+) x (\d+)", text)[-1])
            layout = ini.read_text(encoding="utf8")
            x, y, outer_w, outer_h = map(int, re.search(r"\[Window\]\[场景视口\]\s+Pos=(\d+),(\d+)\s+Size=(\d+),(\d+)", layout).groups())
            pad = (outer_w-width)//2
            viewport = (x+pad, y+outer_h-pad-height, x+pad+width, y+outer_h-pad)
            win32gui.PostMessage(hwnd, win32con.WM_SETFOCUS, 0, 0)
            if physics:
                centers = {"before": record("before")}
                toggle()
                time.sleep(2.0)
                centers["simulated"] = record("simulated")
                toggle()
                centers["stopped"] = record("stopped")
                extent = height if physics == "drop" else width
                assert centers["simulated"] - centers["before"] > .04*extent, centers
                assert abs(centers["stopped"] - centers["before"]) < .01*extent, centers
            else:
                toggle()
                centers = {"idle": record("idle")}
                key(win32con.VK_LEFT, True)
                time.sleep(.38)
                centers["walking"] = record("walking")
                key(win32con.VK_LEFT, False)
                time.sleep(.65)
                centers["released"] = record("released")
                toggle()
                centers["stopped"] = record("stopped")
                assert centers["idle"] - centers["released"] > .055*width, centers
                assert abs(centers["stopped"]-centers["idle"]) < .05*width, centers
        finally:
            if hwnd: win32gui.PostMessage(hwnd, win32con.WM_CLOSE, 0, 0)
            if win32event.WaitForSingleObject(proc, 12000) == win32con.WAIT_TIMEOUT:
                win32process.TerminateProcess(proc, 1)
            result = win32process.GetExitCodeProcess(proc)
            proc.Close()
            desktop.CloseDesktop()
    text = log.read_text(encoding="utf8")
    assert result == 0 and not re.search(r"VUID-|Validation Error|layer is not installed", text), text
    assert text.count("KeyPressedEvent:294") == 2 and text.count("KeyPressedEvent:263") == (0 if physics else 1)
    report = dict(executable_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(), scene=scene.name,
                  gpu=re.search(r"Vulkan device: (.+)", text)[1], viewport_pixels=[width,height],
                  input=["F5", "Left down", "Left up", "F5"], actor_centroid_x_pixels=centers,
                  validation_errors=[], exit_code=result)
    if physics:
        report["input"] = ["F5", "wait 2 seconds", "F5"]
        report["walk_enabled"] = physics == "walk"
        report["starting_pelvis_height_m"] = .91 if physics == "walk" else 1.91
        if physics == "drop": report["actor_centroid_y_pixels"] = report.pop("actor_centroid_x_pixels")
    (out / (prefix + ".json")).write_text(json.dumps(report, indent=2)+"\n", encoding="utf8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exe", type=Path, help="已构建的正式 RedflagForge.exe，旁边应有同一配置的 DLL")
    parser.add_argument("--physics", action="store_true", help="实拍双腿角色前进、放松落地和停止模拟")
    args = parser.parse_args()
    ctypes.windll.user32.SetProcessDPIAware()
    for action in (("walk", "drop") if args.physics else (None,)):
        capture(args.exe.resolve(strict=True), ROOT / "output/validation/character-animation", action)
