"""从正式 ZIP 打开维修站，向游戏送键鼠输入，按游戏帧拍摄窗口并保存玩家与瓦特的状态。"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import zipfile

import pywintypes
import win32con
import win32event
import win32gui
import win32process
import win32service

from check_player_resolution import until

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts/redflag_mcp"))
from playtest import begin, current, issue, write_json
from playtest_capture import client_image

NODES = ["hips", "chest"] + [bone + side for side in (".L", ".R")
    for bone in ("shoulder", "upper_arm", "forearm", "hand", "f_middle.01", "thigh", "knee", "shin", "foot", "toe")]

# 侧向走时摄影机能看到前后两只脚；急转后向前走，让瓦特离开出生点并追上玩家。
INPUTS = [
    (.10, "orbit", "move", (960, 460)), (.22, "orbit", "hold", "MouseRight"),
    (.42, "orbit", "move", (655, 460)), (.72, "orbit", "release", "MouseRight"),
    (1.35, "punch", "tap", "F"), (2.55, "punch-repeat", "tap", "F"),
    (3.65, "walk", "hold", "D"), (4.68, "run", "hold", "Shift"),
    (5.49, "turn", "release", "D"), (5.55, "turn", "hold", "W"),
    (6.15, "stop", "release", "W"), (6.22, "stop", "release", "Shift"),
    (7.35, "jump", "tap", "Space"), (9.0, "crouch", "hold", "Ctrl"),
    (9.65, "stand", "release", "Ctrl")]


def digest(path):
    with path.open("rb") as file:
        return hashlib.file_digest(file, "sha256").hexdigest()


def record(archive, output, render_size, native, profile):
    output.mkdir(parents=True, exist_ok=False)
    pictures = output / "images"
    pictures.mkdir()
    with tempfile.TemporaryDirectory(prefix="redflag-motion-") as temporary:
        project = Path(temporary)
        with zipfile.ZipFile(archive) as package:
            package.extractall(project)
        exe = native.resolve(strict=True) if native else project / "RedflagForge.exe"
        info = begin(project, "assets/scenes/Watt-Station.redflag", "走、跑、转向和出拳的实际姿势", launch=False)
        session = project / "captures/playtests" / info["session"]
        info["pose_nodes"] = NODES
        write_json(session / "session.json", info)
        env = dict(os.environ, REDFLAG_PLAYTEST=str(session))
        args = [str(exe), "--vulkan", "--play", info["scene"], "--render-size", render_size]
        desktop_name = f"Motion{os.getpid()}"
        desktop = win32service.CreateDesktop(desktop_name, 0, win32con.GENERIC_ALL, None)
        startup = win32process.STARTUPINFO()
        startup.lpDesktop = "WinSta0\\" + desktop_name
        proc, thread, pid, _ = win32process.CreateProcess(None, subprocess.list2cmdline(args), None, None,
            False, win32con.CREATE_NO_WINDOW, env, str(project), startup)
        thread.Close()
        journal = []
        sent = []
        actions = INPUTS
        if profile:
            # 第二次 F 开始后再拖镜头。角色沿原方向伸拳，镜头转到侧面可以看到拳头伸出多远。
            actions = sorted(INPUTS + [
                (2.28, "orbit-side", "hold", "MouseRight"), (2.56, "orbit-side", "move", (965,460)),
                (3.09, "orbit-back", "move", (655,460)), (3.32, "orbit-back", "release", "MouseRight")])
        try:
            def window():
                try: handles = desktop.EnumDesktopWindows()
                except pywintypes.error: return None
                return next((int(h) for h in handles if win32process.GetWindowThreadProcessId(h)[1] == pid
                             and win32gui.GetWindowText(h) == "Redflag Game"), None)
            hwnd = until(window)
            until(lambda: (current(session).get("frame") or {}).get("seq", 0) >= 8)
            origin = current(session)["frame"]["seconds"]
            last = -1
            while True:
                if win32process.GetExitCodeProcess(proc) != 259:
                    raise RuntimeError("游戏提前退出")
                frame = current(session)["frame"]
                if frame["seq"] == last:
                    time.sleep(.004)
                    continue
                last = frame["seq"]
                elapsed = frame["seconds"] - origin
                if len(sent) < len(actions) and elapsed >= actions[len(sent)][0]:
                    _, phase, op, key = actions[len(sent)]
                    options = dict(x=key[0], y=key[1]) if op == "move" else dict(key=key)
                    if op == "hold":
                        options["lease_ms"] = 5000
                    command = issue(session, 1, op, **options)
                    sent.append(dict(phase=phase, requested_seq=last, **command))
                # 记录抓取前后两个帧号。窗口呈现、状态文件与 PrintWindow 不共用同一道同步信号。
                picture = client_image(hwnd)
                end = current(session)["frame"]["seq"]
                picture.thumbnail((1280, 720))
                picture.save(pictures / f"{last:05}.jpg", quality=91)
                journal.append(dict(seq=last, seq_end=end, elapsed=elapsed))
                if elapsed >= 10.8: break
            issue(session, 1, "end")
            if win32event.WaitForSingleObject(proc, 25000) != win32con.WAIT_OBJECT_0:
                raise TimeoutError("游戏没有按回执关闭")
            assert win32process.GetExitCodeProcess(proc) == 0
        finally:
            if win32process.GetExitCodeProcess(proc) == 259:
                win32process.TerminateProcess(proc, 1)
                win32event.WaitForSingleObject(proc, 25000)
            proc.Close()
            desktop.CloseDesktop()
        shutil.copy2(session / "frames.jsonl", output / "frames.jsonl")
        for command in sent:
            receipt = json.loads((session / "receipts" / f"{command['id']:06}.json").read_bytes())
            assert receipt["ok"], receipt
            command["applied_seq"] = receipt["seq"]
        write_json(output / "take.json", dict(archive=archive.name, archive_sha256=digest(archive),
            exe_sha256=digest(exe), replaced_native=bool(native), scene_sha256=info["scene_sha256"],
            gameplay_sha256=digest(project / "Resources/Scripts/Sandbox.dll"), render_size=render_size,
            origin=origin, profile_second_punch=profile, inputs=sent, captures=journal))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--native", type=Path, help="只作附加骨骼诊断，以另一版原生程序运行包中的旧资产和旧脚本")
    parser.add_argument("--profile",action="store_true",help="第二次出拳时按住右键环视，额外拍一次侧面")
    parser.add_argument("--render-size", choices=["1920x1080", "1280x720"], default="1280x720")
    args = parser.parse_args()
    ctypes.windll.user32.SetProcessDPIAware()
    record(args.archive.resolve(strict=True), args.output.resolve(), args.render_size, args.native, args.profile)


if __name__ == "__main__":
    main()
