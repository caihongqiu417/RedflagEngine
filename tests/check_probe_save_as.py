"""在独立的 Windows 桌面用编辑器另存灯光工作室，并检查保存失败时的文件。"""
import argparse
import base64
import hashlib
import json
import msvcrt
import os
from pathlib import Path
import shutil
import stat
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

from check_probe_baker import BIN, FORGE, ENTRY, ROOT, dump_yaml
from profile_native_navigation import prepare


def wait_for(condition):
    end = time.monotonic() + 20
    while time.monotonic() < end:
        result = condition()
        if result:
            return result
        time.sleep(.05)
    raise TimeoutError(condition)


def save(exe, work, env, destination=None, before_save=None, retry=False):
    name = f"ProbeSaveAs{os.getpid()}"
    desktop = win32service.CreateDesktop(name, 0, wc.GENERIC_ALL, None)
    start = win32process.STARTUPINFO()
    start.lpDesktop = "WinSta0\\" + name
    start.dwFlags = wc.STARTF_USESTDHANDLES
    log = work / ((destination.stem if destination else "read-only") + ".log")
    try:
        with log.open("wb") as output, open(os.devnull, "rb") as stdin:
            start.hStdOutput = start.hStdError = msvcrt.get_osfhandle(output.fileno())
            start.hStdInput = msvcrt.get_osfhandle(stdin.fileno())
            for handle in (start.hStdOutput, start.hStdInput):
                os.set_handle_inheritable(int(handle), True)
            proc, thread, pid, _ = win32process.CreateProcess(None, subprocess.list2cmdline([str(exe), "--vulkan"]),
                None, None, True, wc.CREATE_NO_WINDOW, env, str(work), start)
            thread.Close()
            try:
                def window(match):
                    return next((int(h) for h in desktop.EnumDesktopWindows() if
                                 win32process.GetWindowThreadProcessId(int(h))[1] == pid and match(int(h))), None)

                wait_for(lambda: b"Opened assets/scenes" in log.read_bytes())
                hwnd = wait_for(lambda: window(lambda h: win32gui.GetWindowText(h) == "Redflag Forge"))
                time.sleep(.8)
                if before_save:
                    wait_for(lambda: b"Lighting preview " in log.read_bytes())
                    before_save()
                def shortcut(choose):
                    keys = [(wc.VK_CONTROL, 0x1d)]
                    if choose:
                        keys.append((wc.VK_SHIFT, 0x2a))
                    keys.append((ord("S"), 0x1f))
                    win32gui.PostMessage(hwnd, wc.WM_SETFOCUS, 0, 0)
                    for key, scan in keys:
                        win32gui.PostMessage(hwnd, wc.WM_KEYDOWN, key, 1 | (scan << 16))
                    time.sleep(.2)
                    for key, scan in reversed(keys):
                        win32gui.PostMessage(hwnd, wc.WM_KEYUP, key, 1 | (scan << 16) | (3 << 30))
                shortcut(bool(destination))
                if destination:
                    dialog = wait_for(lambda: window(lambda h: win32gui.GetClassName(h) == "#32770"))
                    def filename_edit():
                        edits = []
                        def find_edit(h, _):
                            if win32gui.GetClassName(h) == "Edit" and win32gui.IsWindowVisible(h):
                                edits.append(h)
                            return True
                        win32gui.EnumChildWindows(dialog, find_edit, None)
                        return edits[0] if len(edits) == 1 else None
                    win32gui.SendMessage(wait_for(filename_edit), wc.WM_SETTEXT, 0, str(destination))
                    win32gui.PostMessage(dialog, wc.WM_COMMAND, wc.IDOK, 0)
                wait_for(lambda: log.read_bytes().count(b"Editor: ") >= 3)
                if retry:
                    shortcut(False)
                    wait_for(lambda: log.read_bytes().count(b"Editor: ") >= 4)
                win32gui.PostMessage(hwnd, wc.WM_CLOSE, 0, 0)
                assert win32event.WaitForSingleObject(proc, 15000) == wc.WAIT_OBJECT_0
                assert win32process.GetExitCodeProcess(proc) == 0
            finally:
                if win32event.WaitForSingleObject(proc, 0) == wc.WAIT_TIMEOUT:
                    win32process.TerminateProcess(proc, 1)
                    win32event.WaitForSingleObject(proc, 5000)
                proc.Close()
    finally:
        desktop.CloseDesktop()
    return log.read_bytes().decode("utf8", errors="replace")


def run(exe, work):
    prepare(work)
    source = FORGE / "assets/scenes/Light-Studio.redflag"
    entry = work / ENTRY
    settings = Path(str(entry) + ".render.yaml")
    probes = Path(str(entry) + ".rfprobes")
    config = yaml.safe_load(Path(str(source) + ".render.yaml").read_bytes())
    config.update(Shading="Base color", PreviewWidth=160)
    settings.write_bytes(dump_yaml(config))
    original = yaml.safe_load(source.read_bytes())
    model = next(e["ModelRendererComponent"] for e in original["Entities"] if "ModelRendererComponent" in e)
    local_model = work / "LightPanel.gltf"
    shutil.copyfile(FORGE / model["FilePath"], local_model)
    model.update(FilePath=local_model.name, ModelID=0)
    original_bytes = dump_yaml(original)
    entry.write_bytes(original_bytes)
    env = dict(os.environ, PATH=str(BIN) + os.pathsep + os.environ["PATH"])
    with (work / "bake.log").open("wb") as log:
        subprocess.run([str(exe), "--bake-probes", str(ENTRY)], cwd=work, env=env, check=True,
                       stdout=log, stderr=subprocess.STDOUT, timeout=120, creationflags=subprocess.CREATE_NO_WINDOW)
    reference = probes.read_bytes()
    results = {}
    for name in ("same", "moved", "empty", "edited_asset"):
        scene = yaml.safe_load(original_bytes)
        if name == "moved":
            scene["Entities"][1]["TransformComponent"]["Translation"][0] += 1
        if name == "empty":
            scene["Entities"] = [e for e in scene["Entities"] if "CameraComponent" in e]
        entry.write_bytes(dump_yaml(scene))
        destination = work / (name + ".redflag")
        callback = None
        if name == "edited_asset":
            settings.write_bytes(dump_yaml(dict(config, Shading="Lit")))
            # 直到面板读过原来的烘焙才改灯牌文件，保留内存里上一版模型和探针。
            def change_model():
                data = json.loads(local_model.read_bytes())
                uri, encoded = data["buffers"][0]["uri"].split(",", 1)
                vertices = bytearray(base64.b64decode(encoded))
                struct.pack_into("<f", vertices, 0, -0.75)
                data["buffers"][0]["uri"] = uri + "," + base64.b64encode(vertices).decode()
                local_model.write_text(json.dumps(data), encoding="utf8")
            callback = change_model
        result = save(exe, work, env, destination, callback)
        assert "Saved scene and rendering settings" in result and destination.is_file()
        saved = Path(str(destination) + ".rfprobes")
        results[name] = saved.read_bytes() == reference if saved.exists() else False

    # Windows 仍允许写旁边的设置文件。只有场景文件只读时，编辑器应报告失败并留下原有的三个文件。
    entry.write_bytes(original_bytes)
    settings.write_bytes(dump_yaml(config))
    before = [p.read_bytes() for p in (entry, settings, probes)]
    entry.chmod(stat.S_IREAD)
    try:
        result = save(exe, work, env)
    finally:
        entry.chmod(stat.S_IREAD | stat.S_IWRITE)
    results["read_only_reported_success"] = "Saved scene and rendering settings" in result
    results["read_only_preserved_files"] = before == [p.read_bytes() for p in (entry, settings, probes)]
    results["original_probes_preserved"] = probes.read_bytes() == reference

    partial = work / "settings-locked.redflag"
    locked = Path(str(partial) + ".render.yaml")
    locked.write_bytes(b"# original settings\n")
    locked.chmod(stat.S_IREAD)
    try:
        result = save(exe, work, env, partial, retry=True)
    finally:
        locked.chmod(stat.S_IREAD | stat.S_IWRITE)
    results["settings_failure_reported_scene_written"] = "已写入场景" in result and partial.is_file()
    results["settings_failure_preserved_target"] = locked.read_bytes() == b"# original settings\n"
    results["retry_saved_original_scene"] = ("Saved scene and rendering settings to " + ENTRY.as_posix()
                                            in result.replace("\\", "/"))
    return dict(results=results, reference_sha256=hashlib.sha256(reference).hexdigest(),
                exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(), scene=source.name,
                views={"same": "Base color", "moved": "Base color", "empty": "Base color", "edited_asset": "Lit"},
                probe_bytes=len(reference), keys="Ctrl+Shift+S opens Save As. Ctrl+S retries after a failed Save As.",
                comparison="True under same/moved/empty/edited_asset means the destination received the original probe bytes.",
                source_scene_error=next((s.split("Editor: ")[1] for s in (work / "read-only.log").read_text(
                    encoding="utf8", errors="replace").splitlines() if "无法保存场景" in s), None),
                target_settings_error=next((s.split("Editor: ")[1] for s in result.splitlines() if "已写入场景" in s), None))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=BIN / "RedflagForge.exe")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    folder = ROOT / "tmp/probe-baker"
    folder.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="save-as-", dir=folder))
    print(work, flush=True)
    report = run(args.exe.resolve(strict=True), work)
    summary = json.dumps(report, ensure_ascii=False, indent=2) + "\n"
    print(summary)
    (work / "results.json").write_text(summary, encoding="utf8")
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(summary, encoding="utf8")
    assert report["results"] == dict(same=True, moved=False, empty=False, edited_asset=False,
        read_only_reported_success=False, read_only_preserved_files=True, original_probes_preserved=True,
        settings_failure_reported_scene_written=True, settings_failure_preserved_target=True,
        retry_saved_original_scene=True)
    for message in (report["source_scene_error"], report["target_settings_error"]):
        assert "请检查文件是否只读" in message and "\ufffd" not in message
