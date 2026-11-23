"""Forge 与 MCP 用同一套 Debug x64 工程。每次构建或检查保存独立日志和退出码。"""
from collections import deque
from contextlib import ExitStack
import errno
import math
import os
from pathlib import Path
import re
import subprocess
import sys
import time
from uuid import uuid4

from playtest import read_json, write_json, writer_lock

# 通过解决方案构建 Forge，确保它依赖的 C# 游戏程序集也参与本次构建。
PROJECTS = {"Redflag-ScriptCore": r"Core\Redflag-ScriptCore", "SandboxProject": r"Core\SandboxProject",
            "Redflag": r"Core\Redflag", "RedflagForge": r"Tools\RedflagForge"}
CHECKS = {"build_npc_tool": "scripts/redflag_mcp/build_npc_tool.cmd", "build_asset_index": "scripts/build_asset_index.cmd",
          "check_playtest_contract": "tests/check_playtest_contract.py", "check_asset_bundle": "tests/check_asset_bundle.py",
          "check_dev_context": "tests/check_dev_context.py"}
HIDDEN = getattr(subprocess, "CREATE_NO_WINDOW", 0)


def msbuild() -> Path:
    locator = Path(os.environ["ProgramFiles(x86)"]) / "Microsoft Visual Studio/Installer/vswhere.exe"
    result = subprocess.check_output([str(locator), "-latest", "-products", "*", "-requires", "Microsoft.Component.MSBuild",
                                      "-utf8", "-find", r"MSBuild\Current\Bin\amd64\MSBuild.exe"],
                                     encoding="utf-8-sig", creationflags=HIDDEN)
    for path in result.splitlines():
        if Path(path).is_file():
            return Path(path)
    raise ValueError("未找到 x64 MSBuild。请检查 Visual Studio 的构建工具。")


def command(engine, target):
    if target in PROJECTS:
        return [str(msbuild()), str(engine / "Redflag.sln"), "/t:" + PROJECTS[target],
                "/p:Configuration=Debug", "/p:Platform=x64", "/m:4", "/nr:false", "/nologo", "/v:minimal"]
    script = CHECKS[target]
    if script.endswith(".cmd"):
        # 工作目录是引擎根目录；cmd 只接收上表中固定的脚本名。
        return [os.environ["ComSpec"], "/d", "/c", script.replace("/", "\\")]
    return [sys.executable, "-X", "utf8", str(engine / script)]


def output(engine):
    return engine / "bin-int/redflag-mcp-build"


def worker(engine, target, run_id):
    root = output(engine)
    receipt = root / run_id / "status.json"
    with ExitStack() as locks:
        try:
            locks.enter_context(writer_lock(root, wait=False))
        except OSError as error:
            if error.errno not in (errno.EACCES, errno.EAGAIN):
                raise
            owner = root / "active.json"
            current = read_json(owner) if owner.exists() else {}
            write_json(receipt, {"status": "busy", "active": current})
            return

        info = {"project": target, "run_id": run_id}
        write_json(root / "active.json", info)
        write_json(receipt, {"status": "running", "exit_code": None})
        write_json(root / (target + ".json"), info)
        try:
            with (receipt.parent / "output.log").open("wb") as stream:
                code = subprocess.call(command(engine, target), cwd=engine, stdin=subprocess.DEVNULL,
                                       stdout=stream, stderr=subprocess.STDOUT, creationflags=HIDDEN)
            result = {"status": "succeeded" if code == 0 else "failed", "exit_code": code}
        except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
            result = {"status": "failed", "exit_code": None, "message": str(error)}
        write_json(receipt, result)


def check_wait(seconds):
    if not math.isfinite(seconds) or not 0 <= seconds <= 120:
        raise ValueError("wait_seconds 的范围是 0 到 120 秒。")


def start(engine, target, allowed, wait_seconds):
    check_wait(wait_seconds)
    if target not in allowed:
        raise ValueError("可用名称：" + "、".join(allowed))
    run_id = uuid4().hex
    folder = output(engine) / run_id
    folder.mkdir(parents=True)
    write_json(folder / "request.json", {"project": target, "run_id": run_id})
    # 子进程负责写最终回执。关闭发起构建的客户端后，其他客户端仍可凭编号回读。
    with (folder / "worker.log").open("wb") as log:
        process = subprocess.Popen([sys.executable, str(Path(__file__).resolve()), str(engine), target, run_id],
                                   cwd=engine, stdin=subprocess.DEVNULL, stdout=log, stderr=log, creationflags=HIDDEN)
    receipt = folder / "status.json"
    while not receipt.exists():
        if process.poll() is not None and not receipt.exists():
            raise ValueError("构建进程未能启动，见 " + str(folder / "worker.log"))
        time.sleep(.05)
    state = read_json(receipt)
    if state["status"] == "busy":
        owner = state["active"]
        if owner:
            raise ValueError(f"工作区正在运行 {owner['project']}。可用 read_build 查看编号 {owner['run_id']}。")
        raise ValueError("另一项构建或检查正在启动，请稍后重试。")
    return read(engine, target, wait_seconds, run_id)


def state_for(root, run_id):
    receipt = root / run_id / "status.json"
    state = read_json(receipt)
    if state["status"] != "running":
        return state

    # 工作进程异常退出时系统会释放文件锁。旧回执不能永远显示为正在编译。
    with ExitStack() as locks:
        try:
            locks.enter_context(writer_lock(root, wait=False))
            abandoned = True
        except OSError as error:
            if error.errno not in (errno.EACCES, errno.EAGAIN):
                raise
            abandoned = read_json(root / "active.json")["run_id"] != run_id
        if abandoned:
            state = read_json(receipt)
            if state["status"] == "running":
                state = {"status": "interrupted", "exit_code": None, "message": "工作进程已退出，没有收到构建退出码。"}
                write_json(receipt, state)
    return state


def read(engine: Path, project: str, wait_seconds=0., run_id=""):
    check_wait(wait_seconds)
    if project not in PROJECTS and project not in CHECKS:
        raise ValueError("没有这个工程或检查。")
    root = output(engine)
    if not run_id:
        latest = root / (project + ".json")
        if not latest.is_file():
            raise ValueError("还没有运行过 " + project + "。")
        run_id = read_json(latest)["run_id"]
    if not re.fullmatch(r"[0-9a-f]{32}", run_id):
        raise ValueError("构建编号无效。")
    folder = root / run_id
    if not (folder / "status.json").is_file():
        raise ValueError("找不到这次构建。")
    info = read_json(folder / "request.json")
    if info["project"] != project:
        raise ValueError("这个编号属于另一个工程或检查。")

    deadline = time.monotonic() + wait_seconds
    while True:
        state = state_for(root, run_id)
        remaining = deadline - time.monotonic()
        if state["status"] != "running" or remaining <= 0:
            break
        time.sleep(min(remaining, .2))

    log = folder / "output.log"
    errors, warnings, tail = [], 0, deque(maxlen=12)
    if log.exists():
        with log.open("rb") as stream:
            for raw in stream:
                try:
                    line = raw.decode("utf-8").rstrip()
                except UnicodeDecodeError:
                    line = raw.decode("mbcs" if os.name == "nt" else "utf8", errors="replace").rstrip()
                tail.append(line[:400])
                warnings += ": warning " in line
                if (": error " in line or ": fatal error " in line) and len(errors) < 8:
                    short = line.strip()[:500]
                    if short not in errors:
                        errors.append(short)
    return {**info, **state, "log": log.relative_to(engine).as_posix(),
            "errors": errors, "warnings": warnings, "tail": list(tail)}


if __name__ == "__main__":
    workspace, name, ticket = sys.argv[1:]
    worker(Path(workspace), name, ticket)
