"""从空目录启动玩家拿到的压缩包，通过正式窗口给瓦特下指令。"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import statistics
import subprocess
import sys
import tempfile
import time
from zipfile import ZipFile

import win32con
import win32gui

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts/redflag_mcp"))
import playtest
from playtest_capture import client_image, game_window


def unpack(archive, project):
    with ZipFile(archive) as package:
        manifest = json.loads(package.read("asset-manifest.json"))
        members = {row["path"] for row in manifest["files"]}
        assert len(members) == len(manifest["files"])
        assert sorted(package.namelist()) == sorted(members | {"asset-manifest.json"})
        for name in package.namelist():
            if not (project / name).resolve().is_relative_to(project):
                raise ValueError(name)
        package.extractall(project)
    for row in manifest["files"]:
        path = project / row["path"]
        with path.open("rb") as stream:
            assert hashlib.file_digest(stream, "sha256").hexdigest() == row["sha256"], path
    return manifest


def until(check, timeout=45):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        result = check()
        if result:
            return result
        time.sleep(.05)
    raise TimeoutError(check.__name__)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--input-at", type=int, nargs=2, metavar=("X", "Y"))
    parser.add_argument("--mission", action="store_true", help="通过通讯让瓦特修理发电机，两人撤离后按 R 开新局")
    parser.add_argument("--defeat", action="store_true", help="撤离后的第二局体验倒地、任务失败和重新开局")
    parser.add_argument("--use-720p", action="store_true", help="从玩家能双击的 Play-720p.cmd 启动")
    args = parser.parse_args()
    if args.mission and not args.input_at:
        parser.error("--mission 需要 --input-at 指定通讯窗口输入框的位置")
    if args.defeat and not args.mission:
        parser.error("--defeat 需要 --mission")
    ctypes.windll.user32.SetProcessDPIAware()
    args.output.mkdir(parents=True, exist_ok=True)
    extraction = tempfile.TemporaryDirectory(prefix="redflag-station-", ignore_cleanup_errors=True)
    project = Path(extraction.name)
    manifest = unpack(args.archive, project)
    intent = "和瓦特合作修复设备，撤离并重新开局。" if args.mission else "找到瓦特，向它要药并核对双方库存。"
    session = playtest.begin(project, manifest["scene"], intent, launch=False)
    folder = playtest.run_folder(project, session["session"])
    print(project, flush=True)

    def state():
        return playtest.current(folder)

    def command(op, key="", **kwargs):
        ticket = playtest.issue(folder, state()["control"]["generation"], op, key, **kwargs)
        receipt = playtest.await_receipt(folder, ticket["id"])
        assert receipt["ok"] is True, receipt
        return receipt

    def team():
        actors = {a["name"]: a for a in state()["frame"]["actors"]}
        return {name: actors[name] for name in ("玩家", "瓦特")}

    def advance(frames):
        first = state()["frame"]["seq"]
        until(lambda: state()["frame"]["seq"] >= first + frames, 45)

    def screenshot(name):
        advance(4)
        picture = client_image(hwnd)
        assert picture.size == (1920, 1080), picture.size
        picture.save(args.output / (name + ".png"))

    def say(message):
        x, y = args.input_at
        command("move", x=x, y=y)
        command("tap", "MouseLeft")
        advance(3)
        for char in message:
            win32gui.PostMessage(hwnd, win32con.WM_CHAR, ord(char), 1)
        advance(2)
        command("tap", "Enter")

    env = dict(os.environ, REDFLAG_PLAYTEST=str(folder))
    launcher = "Play-720p.cmd" if args.use_720p else "Play-With-Companion.cmd"
    with (project / "launcher.log").open("wb") as log:
        launch = subprocess.Popen(["cmd", "/d", "/c", launcher], cwd=project, env=env,
                                  stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT,
                                  creationflags=subprocess.CREATE_NO_WINDOW)
    hwnd = None
    try:
        until(lambda: state()["frame"], 120)
        hwnd = until(lambda: game_window(state()["status"]["pid"]))
        before = team()
        screenshot("station")
        command("tap", "T")
        screenshot("companion")

        # 这里通过 Windows 的文字输入消息走和输入法相同的窗口入口。玩家实际收到药才通过。
        if args.mission:
            from station_mission import play_station
            mission = play_station(state, command, say, screenshot, folder, args.output,
                                   project / manifest["scene"], args.defeat)
        elif args.input_at:
            say("请把你背包里的一份医疗包送到我手上。")

            def handed_over():
                actors = team()
                return (actors["玩家"]["inventory"].get("medkit", 0) == before["玩家"]["inventory"]["medkit"] + 1
                        and actors["瓦特"]["inventory"].get("medkit", 0) == before["瓦特"]["inventory"]["medkit"] - 1)

            until(handed_over, 45)
            screenshot("delivery")
        with args.archive.open("rb") as archive:
            archive_sha = hashlib.file_digest(archive, "sha256").hexdigest()
        record = dict(scene=manifest["scene"], verified_files=len(manifest["files"]), client=[1920, 1080], launcher=launcher,
                      archive_sha256=archive_sha, exe_sha256=hashlib.sha256((project/"RedflagForge.exe").read_bytes()).hexdigest(),
                      before=before, after=team(), delivered=bool(args.input_at and not args.mission))
        if args.mission:
            record["mission"] = mission
    finally:
        if hwnd and win32gui.IsWindow(hwnd):
            win32gui.PostMessage(hwnd, win32con.WM_CLOSE, 0, 0)
        launch.wait(timeout=15)
        for name in ("frames.jsonl", "jobs.jsonl", "status.json", "session.json"):
            if (folder / name).is_file():
                shutil.copyfile(folder / name, args.output / name)
        shutil.copyfile(project / "launcher.log", args.output / "launcher.log")
        extraction.cleanup()
    assert launch.returncode == 0, args.output / "launcher.log"
    samples = [json.loads(line)["frame_ms"] for line in (args.output / "frames.jsonl").read_bytes().splitlines()]
    samples = [ms for ms in samples if ms > 0]
    record["frame_ms"] = {"samples": len(samples), "p50": statistics.median(samples),
                          "p95": statistics.quantiles(samples, n=100, method="inclusive")[94]}
    (args.output / "result.json").write_text(json.dumps(record, ensure_ascii=False, indent=2), encoding="utf8")
    print(args.output, flush=True)


if __name__ == "__main__":
    main()
