"""在独立 Windows 桌面打开场景，用 RenderDoc 保存、回放第 24 帧。"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time


def capture():
    import renderdoc as rd

    work = Path(os.environ["RF_STUDIO_WORK"])
    renderdoc = Path(os.environ["RF_STUDIO_TOOLS"])
    template = Path(os.environ["RF_STUDIO_CAPTURE"]).with_name("studio")
    # 只让编辑器加载 Vulkan 截帧层，回放进程需要使用正常的 Vulkan 设备。
    env = dict(os.environ, VK_ADD_LAYER_PATH=str(renderdoc), VK_INSTANCE_LAYERS="VK_LAYER_RENDERDOC_Capture")
    with (work / "editor.log").open("wb") as out, (work / "capture.log").open("wb") as err:
        command = [str(renderdoc / "renderdoccmd.exe"), "capture", "-d", str(work), "-c", str(template),
                   os.environ["RF_STUDIO_EXE"], "--vulkan", "--frames", "56"]
        launcher = subprocess.run(command, env=env, stdout=out, stderr=err, timeout=45)
    found = re.search(rb"\bID\s+(\d+)", (work / "capture.log").read_bytes())
    target = rd.CreateTargetControl("localhost", int(found.group(1)) if found else launcher.returncode,
                                    "Redflag Studio", True)
    if not target or not target.Connected():
        raise RuntimeError("RenderDoc could not connect to the running editor")
    try:
        target.QueueCapture(24, 1)
        deadline = time.monotonic() + 150
        while target.Connected() and time.monotonic() < deadline:
            message = target.ReceiveMessage(None)
            if message.type == rd.TargetControlMessageType.NewCapture:
                if message.newCapture.frameNumber != 24:
                    raise RuntimeError("Unexpected frame number")
                (work / "frame-path.txt").write_text(message.newCapture.path, encoding="utf8")
                return
            time.sleep(.004)
        raise RuntimeError("The editor ended before frame 24 was captured")
    finally:
        target.Shutdown()


def run_renderdoc(tools, script, root, env):
    import win32con
    import win32event
    import win32process
    import win32service

    name = f"RedflagStudio{os.getpid()}"
    desktop = win32service.CreateDesktop(name, 0, win32con.GENERIC_ALL, None)
    startup = win32process.STARTUPINFO()
    startup.lpDesktop = "WinSta0\\" + name
    command = subprocess.list2cmdline([str(tools / "qrenderdoc.exe"), "--python", str(script)])
    try:
        process, thread, _, _ = win32process.CreateProcess(None, command, None, None, False,
                                                           win32con.CREATE_NO_WINDOW, env, str(root), startup)
        try:
            if win32event.WaitForSingleObject(process, 240000) != win32con.WAIT_OBJECT_0:
                win32process.TerminateProcess(process, 1)
                raise TimeoutError(script.name)
            if win32process.GetExitCodeProcess(process) != 0:
                raise RuntimeError(f"{script.name} failed")
        finally:
            thread.Close()
            process.Close()
    finally:
        desktop.CloseDesktop()


def main():
    import yaml
    from profile_native_navigation import prepare

    root = Path(__file__).resolve().parents[1]
    binaries = root / "bin/Debug-windows-x86_64/RedflagForge"
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=binaries / "RedflagForge.exe")
    parser.add_argument("--renderdoc", type=Path, required=True, help="Directory containing qrenderdoc.exe")
    parser.add_argument("--no-specular-ibl", action="store_true")
    parser.add_argument("--width", type=int, default=800)
    scenes = parser.add_mutually_exclusive_group()
    scenes.add_argument("--baked-bathroom", action="store_true", help="Use the 160-pixel baked DDGI regression scene")
    scenes.add_argument("--scene", type=Path, help="Use this .redflag file and the settings saved beside it")
    args = parser.parse_args()
    exe = args.exe.resolve(strict=True)
    with exe.open("rb") as binary:
        sha = hashlib.file_digest(binary, "sha256").hexdigest()
    name = (f"bathroom-{sha[:12]}-160-baked" if args.baked_bathroom else
            f"studio-{sha[:12]}-{args.width}-{'diffuse' if args.no_specular_ibl else 'lit'}")
    scene_name = "RTXGI-Bathroom.redflag" if args.baked_bathroom else "Camera-Studio.redflag"
    scene = root / "RedflagForge/assets/scenes" / scene_name
    if args.scene:
        scene = args.scene.resolve(strict=True)
        name = f"{scene.stem}-{sha[:12]}-{args.width}"
    work = root / "tmp/material-reference" / name
    prepare(work)
    entry = work / "assets/scenes/Sponza-Atrium.redflag"
    # 编辑器从工作目录打开默认场景。测试只替换临时目录里的副本。
    entry.write_bytes(scene.read_bytes())
    probes = Path(str(scene) + ".rfprobes")
    entry_probes = Path(str(entry) + ".rfprobes")
    if probes.is_file():
        shutil.copyfile(probes, entry_probes)
    else:
        entry_probes.unlink(missing_ok=True)
    settings = yaml.safe_load(Path(str(scene) + ".render.yaml").read_text(encoding="utf8"))
    if args.baked_bathroom:
        settings.update(Shading="Lit", Direct="All point lights", Indirect="DDGI",
                        TAA=False, GTAO=False, SSR=False, FastNavigation=False, PreviewWidth=160)
    elif args.scene:
        settings["PreviewWidth"] = args.width
    else:
        settings.update(PreviewWidth=args.width, SpecularIBL=not args.no_specular_ibl)
    Path(str(entry) + ".render.yaml").write_text(yaml.safe_dump(settings), encoding="utf8")
    recorded = work / "frame-path.txt"
    recorded.unlink(missing_ok=True)

    directory = root / "output/captures/local" / name
    directory.mkdir(parents=True, exist_ok=True)
    tools = args.renderdoc.resolve(strict=True)
    env = dict(os.environ, PATH=str(binaries) + os.pathsep + os.environ["PATH"], RF_VULKAN_VALIDATION="1",
               RF_STUDIO_WORK=str(work), RF_STUDIO_TOOLS=str(tools), RF_STUDIO_EXE=str(exe),
               RF_STUDIO_CAPTURE=str(directory / "studio_frame24.rdc"), RF_STUDIO_EXPORT=str(work))
    run_renderdoc(tools, Path(__file__).resolve(), root, env)
    env["RF_STUDIO_CAPTURE"] = recorded.read_text(encoding="utf8")
    replay = "replay_baked_preview.py" if args.baked_bathroom else "replay_material_studio.py"
    run_renderdoc(tools, root / "tests" / replay, root, env)
    report = json.loads((work / "replay.json").read_text(encoding="utf8"))
    assert report["frame"] == 24 and not report["messages"], report
    if not args.baked_bathroom and not args.scene:
        assert report["pack_event"], report
    print(f"{env['RF_STUDIO_CAPTURE']}\n{work / 'replay.json'}")


if os.environ.get("RF_STUDIO_WORK"):
    try:
        capture()
    except Exception:
        import traceback
        (Path(os.environ["RF_STUDIO_WORK"]) / "error.txt").write_text(traceback.format_exc(), encoding="utf8")
        sys.exit(1)
    sys.exit(0)
if __name__ == "__main__":
    main()
