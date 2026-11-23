"""用发行包实拍蹲伏到倒地。额外的无网格子节点让试玩记录能还原每个关节的矩阵，供独立蒙皮测量。"""

import argparse
import copy
import ctypes
import gzip
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import time
from zipfile import ZipFile

import numpy as np
import pywintypes
import win32con
import win32event
import win32gui
import win32process
import win32service
import yaml
from capture_player_motion import begin, client_image, current, digest, issue, until, write_json

SCENE = "assets/scenes/Watt-Station.redflag"
MODEL = "assets/game/station/crew/Operator.glb"


def mesh_probes(path):
    """三个轴端点和骨骼原点给出世界矩阵。只追加无外观的测量点，保留网格、权重和全部动画轨。"""
    data = path.read_bytes()
    size = struct.unpack_from("<I", data, 12)[0]
    doc = json.loads(data[20 : 20 + size])
    binary = data[28 + size :]

    def values(index):
        entry = doc["accessors"][index]
        view = doc["bufferViews"][entry["bufferView"]]
        dtype = np.dtype({5126: "<f4", 5121: "u1", 5123: "<u2", 5125: "<u4"}[entry["componentType"]])
        width = {"VEC3": 3, "VEC4": 4, "MAT4": 16, "SCALAR": 1}[entry["type"]]
        return np.ndarray(
            (entry["count"], width),
            dtype=dtype,
            buffer=binary,
            offset=view.get("byteOffset", 0) + entry.get("byteOffset", 0),
            strides=(view.get("byteStride", width * dtype.itemsize), dtype.itemsize),
        )

    parts = []
    joints = set()
    for node in doc["nodes"]:
        if "skin" not in node:
            continue
        skin = doc["skins"][node["skin"]]
        bones = np.array(skin["joints"])
        inverse = values(skin["inverseBindMatrices"]).reshape(-1, 4, 4).transpose(0, 2, 1)
        for primitive in doc["meshes"][node["mesh"]]["primitives"]:
            attrs = primitive["attributes"]
            ids = values(attrs["JOINTS_0"])
            w = values(attrs["WEIGHTS_0"])
            pos = values(attrs["POSITION"])
            bound = (
                np.einsum("vkij,vj->vki", inverse[ids], np.column_stack((pos, np.ones(len(pos)))))
                * w[:, :, None]
            )
            bones4 = bones[ids]
            joints.update(bones4[w > 0].tolist())
            # 用影响最大的关节把网格分到左右靴、躯干和髋部；整件模型仍包含所有顶点。
            groups = np.array(
                [doc["nodes"][i]["name"] for i in bones4[np.arange(len(w)), w.argmax(axis=1)]]
            )
            parts.append((bones4, bound, groups))
    names = {}
    for i in sorted(joints):
        node = doc["nodes"][i]
        p = node["name"]
        names[i] = [p]
        for axis, vector in zip("xyz", np.eye(3).tolist()):
            name = f"QA_{i}_{axis}"
            node.setdefault("children", []).append(len(doc["nodes"]))
            doc["nodes"].append({"name": name, "translation": vector})
            names[i].append(name)
    header = json.dumps(doc, separators=(",", ":"), ensure_ascii=False).encode()
    header += b" " * (-len(header) % 4)
    path.write_bytes(
        struct.pack("<4sIIII", b"glTF", 2, 28 + len(header) + len(binary), len(header), 0x4E4F534A)
        + header
        + struct.pack("<II", len(binary), 0x004E4942)
        + binary
    )
    return names, parts


def surface(nodes, names, parts):
    # 试玩记录的世界坐标包含 IK、模型缩放和角色移动，随后按发行模型原有权重蒙皮。
    matrices = np.zeros((max(names) + 1, 3, 4))
    for i, markers in names.items():
        v = np.array([nodes[m] for m in markers])
        matrices[i, :, :3] = (v[1:] - v[0]).T
        matrices[i, :, 3] = v[0]
    points = []
    groups = []
    for ids, bound, labels in parts:
        indices = np.where(np.any(bound != 0, axis=2), ids, 0)
        points.append(np.einsum("vkij,vkj->vi", matrices[indices], bound))
        groups.append(labels)
    points = np.concatenate(points)
    groups = np.concatenate(groups)
    regions = {
        "model": np.ones(len(groups), dtype=bool),
        "torso": np.isin(groups, ["hips", "spine", "chest"]),
        "left_boot": np.isin(groups, ["foot.L", "toe.L", "footcover.L"]),
        "right_boot": np.isin(groups, ["foot.R", "toe.R", "footcover.R"]),
    }
    return {
        label: cloud[cloud[:, 1].argmin()].round(6).tolist()
        for label, mask in regions.items()
        if len(cloud := points[mask])
    }


def stage(project, airborne, original, watt):
    p = project / SCENE
    scene = yaml.safe_load(p.read_bytes())
    actor = {e["Entity"]: e for e in scene["Entities"]}
    x, y, z = actor[1102 if watt else 1101]["TransformComponent"]["Translation"]
    if original:
        return
    cam = copy.deepcopy(actor[1100])
    actor[1100]["CameraComponent"]["Primary"] = False
    del cam["ScriptComponent"]
    cam["Entity"] = 1999991
    cam["TagComponent"]["Tag"] = "侧面摄影机"
    eye = [x + 0.47, 0.67, z + 3.45]
    target = [x + 0.4, 0.82, z]
    if watt:
        eye, target = [x + 2.8, 0.67, z + 2.2], [x, 0.82, z]
    dx, dy, dz = np.subtract(eye, target)
    cam["TransformComponent"].update(
        Translation=eye, Rotation=[-math.atan2(dy, math.hypot(dx, dz)), math.atan2(dx, dz), 0]
    )
    cam["CameraComponent"]["Camera"]["PerspectiveFov"] = 0.77
    scene["Entities"].append(cam)
    # 在出生点演练电网受伤，先留时间拍蹲伏，再用场景已有的放电脚本击倒玩家。
    cable = actor[1113]["TransformComponent"]["Translation"]
    cable[0], cable[2] = x + 0.5, z  # 保留电缆离地高度，射线从可见地面上方发出。
    overrides = dict(
        StartPhase=1.0,
        Cycle=6.0,
        ActiveSeconds=0.3,
        Damage=100.0,
        Radius=1.6,
        JumpClearance=2.0 if airborne else 0.45,
    )
    for f in actor[1113]["ScriptComponent"]["ScriptFields"]:
        if f["Name"] in overrides:
            f["Value"] = overrides[f["Name"]]
    role = project / "assets/ai/watt.npc.yaml"
    npc = yaml.safe_load(role.read_bytes())
    npc.update(IdleIntent="wait", RescueIntent="wait")
    role.write_text(yaml.safe_dump(npc, allow_unicode=True), encoding="utf8")
    p.write_text(yaml.safe_dump(scene, allow_unicode=True, sort_keys=False), encoding="utf8")


def capture(archive, out, airborne, original, watt):
    out.mkdir(parents=True, exist_ok=False)
    images = out / "images"
    images.mkdir()
    with tempfile.TemporaryDirectory(prefix="station-floor-") as tmp:
        project = Path(tmp)
        with ZipFile(archive) as pack:
            pack.extractall(project)
        path = project / MODEL
        source = digest(path)
        stage(project, airborne, original, watt)
        names, parts = mesh_probes(path)
        info = begin(project, SCENE, "侧面复查站姿、蹲姿、蹲走与倒地后的实际接触面", launch=False)
        folder = project / "captures/playtests" / info["session"]
        info["pose_nodes"] = [m for lst in names.values() for m in lst]
        write_json(folder / "session.json", info)
        deskname = f"Ground{os.getpid()}"
        desk = win32service.CreateDesktop(deskname, 0, win32con.GENERIC_ALL, None)
        startup = win32process.STARTUPINFO()
        startup.lpDesktop = "WinSta0\\" + deskname
        cmd = [
            str(project / "RedflagForge.exe"),
            "--vulkan",
            "--play",
            SCENE,
            "--render-size",
            "1280x720",
        ]
        proc, thread, pid, _ = win32process.CreateProcess(
            None,
            subprocess.list2cmdline(cmd),
            None,
            None,
            False,
            win32con.CREATE_NO_WINDOW,
            dict(os.environ, REDFLAG_PLAYTEST=str(folder)),
            str(project),
            startup,
        )
        thread.Close()
        commands = [
            (0.8, "hold", "Ctrl"),
            (2.1, "hold", "W"),
            (2.7, "release", "W"),
            (3.55, "release", "Ctrl"),
        ]
        if watt:
            commands = []
        actor_id = "1102" if watt else "1101"
        tickets = []
        photos = []
        try:

            def window():
                if win32process.GetExitCodeProcess(proc) != 259:
                    raise RuntimeError("游戏提前退出")
                try:
                    windows = desk.EnumDesktopWindows()
                except pywintypes.error:
                    return None
                return next(
                    (
                        int(h)
                        for h in windows
                        if win32process.GetWindowThreadProcessId(h)[1] == pid
                        and win32gui.GetWindowText(h) == "Redflag Game"
                    ),
                    None,
                )

            hwnd = until(window)
            until(lambda: (current(folder).get("frame") or {}).get("seq", 0) >= 6)
            last = -1
            origin = None
            down_at = None
            while True:
                f = current(folder)["frame"]
                seq = f["seq"]
                t = f["seconds"]
                if seq == last:
                    time.sleep(0.004)
                    continue
                last = seq
                if origin is None:
                    origin = t
                    if airborne:
                        # 第一次更新后五秒电缆通电。按游戏时间起跳，给输入一帧时间送达。
                        early = (folder / "frames.jsonl").read_bytes().splitlines()
                        sim_start = next(
                            row["seconds"] for row in map(json.loads, early[:3]) if row["seq"] == 2
                        )
                        commands.append((sim_start + 4.35 - origin, "tap", "Space"))
                elapsed = t - origin
                if len(tickets) < len(commands) and elapsed >= commands[len(tickets)][0]:
                    _, op, key = commands[len(tickets)]
                    tickets.append(
                        issue(folder, 1, op, key, **({"lease_ms": 8000} if op == "hold" else {}))
                    )
                image = client_image(hwnd)
                end = current(folder)["frame"]["seq"]
                image.thumbnail((1280, 720))
                image.save(images / f"{seq:04}.jpg", quality=94)
                photos.append(dict(seq=seq, end=end, time=elapsed))
                player = next(a for a in f["actors"] if a["id"] == actor_id)
                if player["health"] == 0 and down_at is None:
                    down_at = elapsed
                if (
                    (down_at is not None and elapsed - down_at > 2.4)
                    or elapsed > 13.5
                    or (original and elapsed > 4.3)
                ):
                    break
            issue(folder, 1, "end")
            assert win32event.WaitForSingleObject(proc, 25000) == win32con.WAIT_OBJECT_0
        finally:
            if win32process.GetExitCodeProcess(proc) == 259:
                win32process.TerminateProcess(proc, 1)
                win32event.WaitForSingleObject(proc, 20000)
            proc.Close()
            desk.CloseDesktop()
        raw = (folder / "frames.jsonl").read_bytes()
        # 只保存玩家实测骨骼，省去每个非目标角色的数百个坐标。
        rows = []
        evidence = []
        for line in raw.splitlines():
            f = json.loads(line)
            a = next(a for a in f["actors"] if a["id"] == actor_id)
            pose = a["animation"]
            if not pose["nodes"]:
                continue
            evidence.append(dict(seq=f["seq"], player=a))
            rows.append(
                dict(
                    seq=f["seq"],
                    time=f["seconds"] - origin,
                    root=a["position"],
                    health=a["health"],
                    clip=pose["clip"],
                    clip_time=pose["time"],
                    lowest=surface(pose["nodes"], names, parts),
                )
            )
        (out / "bones.jsonl.gz").write_bytes(
            gzip.compress(
                b"\n".join(json.dumps(r, separators=(",", ":")).encode() for r in evidence), mtime=0
            )
        )
        write_json(
            out / "poses.json",
            dict(
                package=digest(archive),
                model=source,
                scene=info["scene_sha256"],
                original=original,
                actor=actor_id,
                airborne=airborne,
                inputs=tickets,
                photos=photos,
                frames=rows,
            ),
        )
        for t in [0.55, 1.8, 2.55, 3.4, 4.5, 5.5, 6.8, 7.6]:
            r = min(rows, key=lambda r: abs(r["time"] - t))
            print(
                r["seq"],
                round(r["time"], 2),
                r["clip"],
                r["root"][1],
                {k: v[1] for k, v in r["lowest"].items()},
                flush=True,
            )


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("archive", type=Path)
    p.add_argument("output", type=Path)
    p.add_argument("--airborne", action="store_true")
    p.add_argument("--original", action="store_true")
    p.add_argument("--watt", action="store_true", help="测未被扶起时瓦特的定格")
    a = p.parse_args()
    ctypes.windll.user32.SetProcessDPIAware()
    capture(a.archive, a.output, a.airborne, a.original, a.watt)
