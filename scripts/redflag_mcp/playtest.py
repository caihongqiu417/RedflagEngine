"""Forge、MCP 和命令行共用的试玩会话。游戏进程负责执行输入，并逐帧写下结果。"""
import argparse
from contextlib import contextmanager
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
from tempfile import NamedTemporaryFile
import time
from uuid import uuid4

from project import ENGINE, PROJECT


def write_json(path: Path, value):
    with NamedTemporaryFile("w", dir=path.parent, delete=False, encoding="utf8") as file:
        temporary = Path(file.name)
        try:
            json.dump(value, file, ensure_ascii=False, allow_nan=False)
            file.write("\n")
        except BaseException:
            file.close()
            temporary.unlink(missing_ok=True)
            raise
    try:
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def read_json(path: Path):
    return json.loads(path.read_bytes())


def append_event(path: Path, row: dict):
    with path.open("ab") as stream:
        stream.write((json.dumps(row,ensure_ascii=False,allow_nan=False) + "\n").encode("utf8"))


@contextmanager
def writer_lock(folder: Path, name=".writer", wait=True):
    # 系统在进程退出时释放文件锁。两个 MCP 客户端同时按键也会取得不同的命令编号。
    with (folder / name).open("a+b") as file:
        file.seek(0)
        if os.name == "nt":
            import msvcrt
            msvcrt.locking(file.fileno(), msvcrt.LK_LOCK if wait else msvcrt.LK_NBLCK, 1)
        else:
            import fcntl
            fcntl.flock(file.fileno(), fcntl.LOCK_EX | (0 if wait else fcntl.LOCK_NB))
        try:
            yield
        finally:
            if os.name == "nt":
                file.seek(0)
                msvcrt.locking(file.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                fcntl.flock(file.fileno(), fcntl.LOCK_UN)


def run_folder(project: Path, session: str) -> Path:
    root = (project / "captures/playtests").resolve()
    folder = (root / session).resolve()
    if folder.parent != root or not (folder / "session.json").is_file():
        raise ValueError("找不到这个项目的试玩记录。")
    return folder


def begin(project: Path, scene: str, intent: str, launch=True, executable=None, pose_nodes=None):
    project = project.resolve()
    path = (project / scene).resolve()
    if path.suffix != ".redflag" or not path.is_relative_to(project / "assets/scenes") or not path.is_file():
        raise ValueError("请选择当前项目 assets/scenes 里的场景。")
    if not intent.strip():
        raise ValueError("请填写这次想体验或验证的内容。")
    exe = Path(executable or ENGINE / "bin/Debug-windows-x86_64/RedflagForge/RedflagForge.exe").resolve()
    if launch and not exe.is_file():
        raise ValueError("请先构建 RedflagForge Debug x64。")
    session = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ-") + uuid4().hex[:10]
    folder = project / "captures/playtests" / session
    (folder / "commands").mkdir(parents=True)
    (folder / "receipts").mkdir()
    write_json(folder / "handoff.json", {"owner":"agent", "generation":1})
    metadata = {"version":1, "session":session, "scene":path.relative_to(project).as_posix(), "intent":intent,
                "scene_sha256":hashlib.sha256(path.read_bytes()).hexdigest(), "created":datetime.now(timezone.utc).isoformat()}
    if pose_nodes:
        metadata["pose_nodes"] = list(dict.fromkeys(pose_nodes))
    write_json(folder / "session.json", metadata)
    if launch:
        args = [str(exe), "--vulkan", "--play", str(path)]
        env = dict(os.environ, REDFLAG_PLAYTEST=str(folder))
        # 日志留在本次会话里。MCP 客户端断开后，开发者仍可以在原窗口接着玩。
        with (folder / "forge.log").open("wb") as log:
            game = subprocess.Popen(args, cwd=project, env=env, stdin=subprocess.DEVNULL, stdout=log,
                                    stderr=subprocess.STDOUT, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        with exe.open("rb") as binary:
            digest = hashlib.file_digest(binary, "sha256").hexdigest()
        write_json(folder / "process.json", {"pid":game.pid, "exe":str(exe), "exe_sha256":digest})
        if os.name == "nt":
            with (folder / "recorder.log").open("wb") as log:
                subprocess.Popen([sys.executable,str(Path(__file__).with_name("playtest_capture.py")),str(folder),str(game.pid)],
                                 stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
    return metadata


def feedback(folder: Path, seq: int, entity: str, text: str):
    last = current(folder)["frame"]
    if not last or seq < 0 or seq > last["seq"] or not text.strip():
        raise ValueError("请选择已记录的帧，并填写意见。")
    note = {"seq":seq, "entity":entity, "text":text, "time":datetime.now(timezone.utc).isoformat()}
    with writer_lock(folder):
        append_event(folder / "feedback.jsonl", note)
    return note


def issue(folder: Path, generation: int, op: str, key="", owner="", x=None, y=None, lease_ms=None):
    if op not in ("hold", "release", "tap", "move", "handoff", "end"):
        raise ValueError("未知的操作。")
    if op in ("hold", "release", "tap") and not key:
        raise ValueError("请给出按键名。")
    if op == "move" and (x is None or y is None):
        raise ValueError("移动鼠标需要客户区的 x 和 y。")
    if op == "handoff" and owner not in ("agent", "human"):
        raise ValueError("接管者应为 agent 或 human。")

    with writer_lock(folder):
        control = read_json(folder / "handoff.json")
        if control["generation"] != generation or (op != "handoff" and control["owner"] != "agent"):
            raise ValueError("操作权已转交。请重新读取本局状态。")
        counter = folder / "next-command.json"
        number = read_json(counter) if counter.exists() else 1
        while (folder / "commands" / f"{number:06}.json").exists() or (folder / "receipts" / f"{number:06}.json").exists():
            number += 1
        if number > 999999:
            raise ValueError("本次记录的命令编号已用完，请另开一局。")
        command = {"id":number, "generation":generation, "op":op}
        if key:
            command["key"] = key
        if op == "handoff":
            command["owner"] = owner
        if x is not None:
            command["x"] = x
        if y is not None:
            command["y"] = y
        if lease_ms is not None:
            if op != "hold" or not 1 <= lease_ms <= 30000:
                raise ValueError("lease_ms 是按住后自动松开的毫秒数，范围为 1 到 30000。")
            command["lease_ms"] = lease_ms
        write_json(folder / "commands" / f"{number:06}.json", command)
        write_json(counter, number + 1)
    return command


def await_receipt(folder: Path, number: int, timeout=12.):
    path = folder / "receipts" / f"{number:06}.json"
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if path.is_file():
            return read_json(path)
        time.sleep(.03)
    raise TimeoutError(f"命令 {number} 仍没有游戏回执。")


def frames(folder: Path, offset=0, limit=60):
    # 返回字节位置。读取方按此继续，不必为看最新几帧反复解析整局。
    rows = []
    path = folder / "frames.jsonl"
    if not path.exists():
        return {"frames":rows, "offset":0}
    with path.open("rb") as file:
        if offset < 0 or offset > path.stat().st_size:
            raise ValueError("无效的回放位置。")
        file.seek(offset)
        for _ in range(max(1,min(limit,600))):
            line = file.readline()
            if not line.endswith(b"\n"):
                break
            rows.append(json.loads(line))
            offset = file.tell()
    return {"frames":rows, "offset":offset}


def current(folder: Path):
    path = folder / "frames.jsonl"
    latest = None
    if path.is_file():
        with path.open("rb") as file:
            end = file.seek(0, os.SEEK_END)
            size = 8192
            while end:
                start = max(0, end - size)
                file.seek(start)
                lines = file.read(end - start).split(b"\n")[:-1]
                if start and lines:
                    lines = lines[1:]
                if lines:
                    latest = json.loads(lines[-1])
                    break
                if not start:
                    break
                size *= 2
    status = folder / "status.json"
    metadata = read_json(folder / "session.json")
    return {"session":metadata, "control":read_json(folder / "handoff.json"), "frame":latest,
            "status":read_json(status) if status.exists() else {"state":metadata.get("status","ready")}}


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    commands = cli.add_subparsers(dest="action", required=True)
    start = commands.add_parser("begin")
    start.add_argument("--scene", required=True)
    start.add_argument("--intent", required=True)
    start.add_argument("--prepare", action="store_true", help="只建立会话，由宿主在同一目录运行 Play")
    action = commands.add_parser("command")
    action.add_argument("--op", required=True)
    action.add_argument("--generation", type=int, required=True)
    action.add_argument("--key", default="")
    action.add_argument("--owner", default="")
    action.add_argument("--x", type=float)
    action.add_argument("--y", type=float)
    action.add_argument("--lease-ms", type=int)
    state = commands.add_parser("status")
    note = commands.add_parser("feedback")
    note.add_argument("--seq",type=int,required=True)
    note.add_argument("--entity",default="")
    note.add_argument("--text",required=True)
    for sub in (start, action, state, note):
        sub.add_argument("--project", type=Path, default=PROJECT)
    for sub in (action,state,note):
        sub.add_argument("--session", required=True)
    args = cli.parse_args()
    if args.action == "begin":
        result = begin(args.project,args.scene,args.intent,not args.prepare)
    else:
        folder = run_folder(args.project,args.session)
        if args.action == "feedback":
            result = feedback(folder,args.seq,args.entity,args.text)
        elif args.action == "status":
            result = current(folder)
        else:
            result = issue(folder,args.generation,args.op,args.key,args.owner,args.x,args.y,args.lease_ms)
    print(json.dumps(result, ensure_ascii=False))


if __name__ == "__main__":
    main()
