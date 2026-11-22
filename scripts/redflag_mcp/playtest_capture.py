"""只记录本次启动的游戏客户区。抽取关键帧供 Forge 回看，完整的操作与状态仍由游戏逐帧写入。"""
import ctypes
from pathlib import Path
import sys
import time

import win32api
import win32con
import win32event
import win32gui
import win32process
import win32ui
from PIL import Image

from playtest import append_event, current, read_json, write_json, writer_lock


def game_window(pid):
    found = []
    def visit(hwnd, _):
        if win32process.GetWindowThreadProcessId(hwnd)[1] == pid and win32gui.GetWindowText(hwnd) in ("Redflag Game", "Redflag Forge"):
            found.append(hwnd)
        return True
    win32gui.EnumWindows(visit, None)
    return found[0] if found else None


def client_image(hwnd):
    _, _, width, height = win32gui.GetClientRect(hwnd)
    handle = win32gui.GetDC(hwnd)
    source = win32ui.CreateDCFromHandle(handle)
    dest = source.CreateCompatibleDC()
    bitmap = win32ui.CreateBitmap()
    try:
        bitmap.CreateCompatibleBitmap(source, width, height)
        previous = dest.SelectObject(bitmap)
        try:
            if not ctypes.windll.user32.PrintWindow(hwnd, dest.GetSafeHdc(), 3):
                raise OSError("无法读取这次游戏的窗口。")
            return Image.frombytes("RGB",(width,height),bitmap.GetBitmapBits(True),"raw","BGRX",0,1)
        finally:
            dest.SelectObject(previous)
    finally:
        dest.DeleteDC()
        win32gui.DeleteObject(bitmap.GetHandle())
        win32gui.ReleaseDC(hwnd, handle)


def record(folder: Path, pid: int):
    ctypes.windll.user32.SetProcessDPIAware()
    process = win32api.OpenProcess(win32con.SYNCHRONIZE | win32con.PROCESS_QUERY_INFORMATION, False, pid)
    images = folder / "images"
    images.mkdir(exist_ok=True)
    next_at = 0.
    last_seq = -1
    try:
        while win32event.WaitForSingleObject(process,200) == win32con.WAIT_TIMEOUT:
            frame = current(folder)["frame"]
            hwnd = game_window(pid)
            now = time.monotonic()
            if not frame or not hwnd or frame["seq"] == last_seq or now < next_at:
                continue
            last_seq = frame["seq"]
            try:
                picture = client_image(hwnd)
            except (OSError, win32gui.error, win32ui.error) as error:
                print(error, flush=True)
                next_at = now + 1.
                continue
            # 保留图片采集前、后的帧号。操作发生在两者之间时，回放者能看到采样的时间误差。
            end = current(folder)["frame"] or frame
            size = picture.size
            picture.thumbnail((1280, 720))
            file = images / f"{last_seq:09}.jpg"
            picture.save(file, quality=85)
            entry = {"seq":last_seq,"seq_end":end["seq"],"seconds":frame["seconds"],"image":file.relative_to(folder).as_posix(),
                     "source":"game client","client_size":size}
            with writer_lock(folder):
                append_event(folder / "media.jsonl", entry)
            next_at = now + 1.5
        status_file = folder / "status.json"
        status = read_json(status_file) if status_file.exists() else {}
        if status.get("state") not in ("stopped","ended"):
            write_json(status_file,{"state":"exited","exit_code":win32process.GetExitCodeProcess(process),"seq":last_seq})
    finally:
        process.Close()


if __name__ == "__main__":
    record(Path(sys.argv[1]), int(sys.argv[2]))
