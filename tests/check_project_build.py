"""复查两个 MCP 进程共享的构建记录。加 --wire 后，实际编译 C# 脚本和 Forge。"""
import asyncio
import json
import os
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory

ROOT = Path(__file__).resolve().parents[1]
MODULE = ROOT / "scripts/redflag_mcp"
sys.path.insert(0, str(MODULE))
import project_build as builds


def rejects(operation):
    try:
        operation()
    except ValueError:
        return
    raise AssertionError("expected a rejected request")


def check_workers(engine):
    (engine / "tests").mkdir()
    target = "check_playtest_contract"
    script = engine / builds.CHECKS[target]
    script.write_text("from pathlib import Path\nimport time\n"
                      "while not Path('release').exists(): time.sleep(.05)\n"
                      "print('builder: error TEST17: 中文回执')\nraise SystemExit(17)\n", encoding="utf8")

    # 发起进程随即退出。另一个 Python 进程仍可查看状态，且不能同时占用同一个输出目录。
    caller = "import project_build as b,sys,json; from pathlib import Path; print(json.dumps(b.start(Path.cwd(),sys.argv[1],b.CHECKS,0)))"
    result = subprocess.check_output([sys.executable, "-c", caller, target], cwd=engine,
                                     env=dict(os.environ, PYTHONPATH=str(MODULE)), encoding="utf8")
    job = json.loads(result)
    run = job["run_id"]
    try:
        assert builds.read(engine, target, 0, run)["status"] == "running"
        rejects(lambda: builds.start(engine, "SandboxProject", builds.PROJECTS, 0))
        rejects(lambda: builds.read(engine, "SandboxProject", 0, run))
        rejects(lambda: builds.read(engine, target, 0, "../outside"))
        rejects(lambda: builds.start(engine, "../other.py", builds.CHECKS, 0))
        rejects(lambda: builds.start(engine, target, builds.CHECKS, float("nan")))
    finally:
        (engine / "release").touch()
        failed = builds.read(engine, target, 15, run)
    assert failed["exit_code"] == 17 and failed["status"] == "failed"
    assert failed["errors"] == ["builder: error TEST17: 中文回执"]

    script.write_text("print('repaired')\n", encoding="utf8")
    repaired = builds.start(engine, target, builds.CHECKS, 15)
    assert repaired["status"] == "succeeded" and repaired["exit_code"] == 0
    assert repaired["run_id"] != run and repaired["log"] != failed["log"]
    assert builds.read(engine, target)["run_id"] == repaired["run_id"]
    assert builds.read(engine, target, 0, run)["errors"] == failed["errors"]

    status = builds.output(engine) / run / "status.json"
    status.write_text('{"status":"running","exit_code":null}', encoding="utf8")
    assert builds.read(engine, target, 0, run)["status"] == "interrupted"


async def check_wire():
    from mcp import ClientSession, StdioServerParameters
    from mcp.client.stdio import stdio_client

    server = StdioServerParameters(command=sys.executable, args=[str(MODULE / "server.py")], env=dict(os.environ))
    # 部分宿主在断开 stdio 时终止整个进程树。无退出码时须记录中断，随后可重试。
    async with stdio_client(server) as first, ClientSession(*first) as client:
        await client.initialize()
        started = await client.call_tool("run_project_check", {"check": "build_npc_tool"})
        assert not started.is_error, started.content
        ticket = started.structured_content["run_id"]
    async with stdio_client(server) as a, ClientSession(*a) as writer, stdio_client(server) as b, ClientSession(*b) as reader:
        await asyncio.gather(writer.initialize(), reader.initialize())
        finished = await reader.call_tool("read_build", {"project": "build_npc_tool", "run_id": ticket, "wait_seconds": 60})
        assert not finished.is_error, finished.content
        observed = finished.structured_content
        if observed["status"] == "interrupted":
            assert observed["exit_code"] is None
            retry = await writer.call_tool("run_project_check", {"check": "build_npc_tool", "wait_seconds": 60})
            assert not retry.is_error and retry.structured_content["exit_code"] == 0
            assert retry.structured_content["run_id"] != ticket
        else:
            assert observed["status"] == "succeeded" and observed["exit_code"] == 0
        print("stdio disconnect", ticket, observed["status"])
        cap = (await reader.call_tool("get_capabilities", {})).structured_content["build"]
        assert set(cap["checks"]) == set(builds.CHECKS)
        for tool, arg, name in [("build_project", "project", "SandboxProject"), ("build_project", "project", "RedflagForge"),
                                ("run_project_check", "check", "check_playtest_contract")]:
            started = await writer.call_tool(tool, {arg: name})
            assert not started.is_error, started.content
            job = started.structured_content
            while True:
                completed = await reader.call_tool("read_build", {"project": name, "run_id": job["run_id"], "wait_seconds": 10})
                assert not completed.is_error, completed.content
                row = completed.structured_content
                if row["status"] != "running":
                    break
            assert row["exit_code"] == 0 and row["status"] == "succeeded", row
            print(name, row["run_id"], row["status"], row["log"])


if __name__ == "__main__":
    with TemporaryDirectory(prefix="Redflag build test ") as folder:
        check_workers(Path(folder))
    if "--wire" in sys.argv:
        asyncio.run(check_wire())
    print("PASS shared build lock, cross-process reconnect, exact exit codes and independent logs")
