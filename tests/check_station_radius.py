"""只修改关卡的胶囊半径，从正常的 W 键输入测到前方墙面的停车距离。"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
from zipfile import ZipFile

import win32con
import win32event
import win32process
import win32service
import yaml

from capture_player_motion import begin, current, digest, issue, until


def run(project, source, radius):
    scene = yaml.safe_load(source)
    player = next(e for e in scene["Entities"] if e["Entity"] == 1101)
    player["CapsuleCollider3DComponent"]["Radius"] = radius
    spawn = player["TransformComponent"]["Translation"]
    # 同一块竖墙放在出生点正前方两米半。慢走能减小最后一步导致的测量误差。
    player["ScriptComponent"]["ScriptFields"].append(dict(Name="MoveSpeed", Type="Float", Value=.9))
    wall_x = spawn[0] + 2.5
    scene["Entities"].append(dict(Entity=1980001, TagComponent=dict(Tag="宽度测试墙"),
        TransformComponent=dict(Translation=[wall_x+.1, 1.5, spawn[2]], Rotation=[0,0,0], Scale=[1,1,1]),
        RigidBody3DComponent=dict(BodyType="Static", Density=1), BoxCollider3DComponent=dict(HalfSize=[.1,1.55,2.])))
    path = "assets/scenes/Watt-Station.redflag"
    (project/path).write_text(yaml.safe_dump(scene, allow_unicode=True, sort_keys=False), encoding="utf8")
    (project/(path+".render.yaml")).write_text("Shading: Base color\n", encoding="utf8")
    info = begin(project, path, "按住 W 走向测试墙", launch=False)
    session = project/"captures/playtests"/info["session"]
    desktop_name = f"PlayerRadius{os.getpid()}"
    desk = win32service.CreateDesktop(desktop_name, 0, win32con.GENERIC_ALL, None)
    startup = win32process.STARTUPINFO()
    startup.lpDesktop = "WinSta0\\" + desktop_name
    command = [str(project/"RedflagForge.exe"), "--vulkan", "--play", str(project/path), "--render-size", "1280x720"]
    proc, thread, _, _ = win32process.CreateProcess(None, subprocess.list2cmdline(command), None, None, False,
        win32con.CREATE_NO_WINDOW, dict(os.environ, REDFLAG_PLAYTEST=str(session)), str(project), startup)
    thread.Close()
    try:
        def frame():
            code = win32process.GetExitCodeProcess(proc)
            if code != 259:
                raise RuntimeError(f"Game exited with status {code}")
            return current(session).get("frame") or {}
        until(lambda: frame().get("seq", 0) >= 8)
        issue(session, 1, "hold", "W", lease_ms=10000)
        origin = current(session)["frame"]["seconds"]
        until(lambda: frame()["seconds"] - origin >= 5.)
        issue(session, 1, "release", "W")
        issue(session, 1, "end")
        assert win32event.WaitForSingleObject(proc, 20000) == win32con.WAIT_OBJECT_0
        assert win32process.GetExitCodeProcess(proc) == 0
    finally:
        if win32process.GetExitCodeProcess(proc) == 259:
            win32process.TerminateProcess(proc, 1)
        proc.Close()
        desk.CloseDesktop()

    frames = [json.loads(line) for line in (session/"frames.jsonl").read_bytes().splitlines()]
    samples = [[f["seq"], next(a["position"][0] for a in f["actors"] if a["id"] == "1101")]
               for f in frames if f["seconds"] >= origin]
    last = samples[-1][1]
    assert last - spawn[0] > 1.5 and abs(samples[-4][1] - last) < .002
    return dict(radius_m=radius, wall_x=wall_x, distance_m=wall_x-last, player_x=samples)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="ground-radius-") as tmp, ZipFile(args.package) as package:
        project = Path(tmp)
        package.extractall(project)
        source = package.read("assets/scenes/Watt-Station.redflag")
        narrow, wide = [run(project, source, radius) for radius in (.20,.52)]
    for case in (narrow, wide):
        assert abs(case["distance_m"] - case["radius_m"]) < .13, case
    assert wide["distance_m"] - narrow["distance_m"] > .20
    report = dict(archive_sha256=digest(args.package), narrow=narrow, wide=wide)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf8")
    print("PASS wall distance", narrow["distance_m"], wide["distance_m"])
