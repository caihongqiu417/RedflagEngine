"""启动正式 Vulkan 编辑器，用 F5 和方向键验证场景里的 BVH 角色，保存视口实拍。"""
import argparse
import ctypes
import hashlib
import json
import msvcrt
import os
from pathlib import Path
import re
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


def capture(exe, out):
    work = Path(tempfile.mkdtemp(prefix="character-input-", dir=ROOT / "tmp"))
    prepare(work)
    out.mkdir(parents=True, exist_ok=True)
    scene = ROOT / "RedflagForge/assets/scenes/Character-Walk.redflag"
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
        panel.save(out / ("control-" + label + ".png"))
        # 骨段是棕色，地面和背景是中性灰。平均横坐标用来比较角色在同一视口中的位置。
        rgb = np.asarray(panel.convert("RGB"), dtype=np.int16)
        y, x = np.where((rgb[:,:,0] > rgb[:,:,1]+8) & (rgb[:,:,1] > rgb[:,:,2]+8))
        assert len(x) > 100, "No visible BVH skeleton"
        return round(float(x.mean()), 2)

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
    assert text.count("KeyPressedEvent:294") == 2 and text.count("KeyPressedEvent:263") == 1
    report = dict(executable_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(), scene=scene.name,
                  gpu=re.search(r"Vulkan device: (.+)", text)[1], viewport_pixels=[width,height],
                  input=["F5", "Left down", "Left up", "F5"], actor_centroid_x_pixels=centers,
                  validation_errors=[], exit_code=result)
    (out / "control.json").write_text(json.dumps(report, indent=2)+"\n", encoding="utf8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exe", type=Path, help="已构建的正式 RedflagForge.exe，旁边应有同一配置的 DLL")
    args = parser.parse_args()
    ctypes.windll.user32.SetProcessDPIAware()
    capture(args.exe.resolve(strict=True), ROOT / "output/validation/character-animation")
