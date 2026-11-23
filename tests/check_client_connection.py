"""从真实客户端配置启动 stdio 服务，检查安装前后的文件，以及场景包内的实际内容。"""
import argparse
import asyncio
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory
import tomllib
import zipfile

from mcp import ClientSession, StdioServerParameters, stdio_client


ROOT = Path(__file__).resolve().parents[1]
CONNECT = ROOT / "scripts/redflag_mcp/connect.py"


def cli(project, *args, success=True):
    run = subprocess.run([sys.executable, "-X", "utf8", str(CONNECT), *args, "--project", str(project)],
                         cwd=project, capture_output=True, encoding="utf8")
    assert (run.returncode == 0) == success, run.stderr
    return run.stdout


def check_config(project):
    server = json.loads(cli(project))["mcpServers"]["redflag"]
    for client in ("claude-code", "claude-desktop", "cursor"):
        assert json.loads(cli(project, "--client", client))["mcpServers"]["redflag"] == server
    assert json.loads(cli(project, "--client", "vscode"))["servers"]["redflag"] == dict(server, type="stdio")
    assert tomllib.loads(cli(project, "--client", "codex"))["mcp_servers"]["redflag"] == dict(server, tool_timeout_sec=180)

    config = project / "desktop.json"
    initial = b'\xef\xbb\xbf{\r\n  "mcpServers": {"other":{"command":"keep"}}, "prefs": {"enabled":true}\r\n}\r\n'
    config.write_bytes(initial)
    options = ("--install", "claude-desktop", "--config", str(config))
    cli(project, *options)
    installed = json.loads(config.read_bytes())
    assert installed["mcpServers"] == {"other": {"command": "keep"}, "redflag": server}
    assert installed["prefs"] == {"enabled": True}
    cli(project, *options)
    backups = list(project.glob("desktop.json.*.bak"))
    assert len(backups) == 1 and backups[0].read_bytes() == initial
    custom = config.read_bytes().replace(b"server.py", b"other.py")
    config.write_bytes(custom)
    cli(project, *options, success=False)
    assert config.read_bytes() == custom
    config.write_bytes(b"{broken")
    cli(project, *options, success=False)
    assert config.read_bytes() == b"{broken"
    config.unlink()
    cli(project, *options)
    assert json.loads(config.read_bytes())["mcpServers"]["redflag"] == server

    # 两个克隆同时登记到一个客户端时，保留先写入的项目。
    other = project / "another game"
    (other / "assets").mkdir(parents=True)
    config.write_bytes(initial)
    installers = [subprocess.Popen([sys.executable, str(CONNECT), *options, "--project", str(game)],
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) for game in (project, other)]
    assert sorted(p.wait(timeout=10) for p in installers) == [0, 1]
    assert json.loads(config.read_bytes())["mcpServers"]["other"] == {"command": "keep"}
    print("PASS all client formats, merge, exact backup, idempotence, conflicting installs and malformed configs")
    return server


async def check_package(project, server, runtime):
    async with stdio_client(StdioServerParameters(**server)) as streams, ClientSession(*streams) as client:
        await client.initialize()
        cap = (await client.call_tool("get_capabilities", {})).structured_content
        assert Path(cap["project"]) == project and "package_scene" in cap["packaging"]["tools"]
        async def package(**args):
            return await client.call_tool("package_scene", dict(scene="Valve.redflag", **args))
        assert (await client.call_tool("package_scene", {"scene": "../Valve.redflag"})).is_error
        result = (await package()).structured_content
        archive = Path(result["archive"])
        assert result["kind"] == "scene_assets" and archive.is_relative_to(project / "output/packages")
        with zipfile.ZipFile(archive) as z:
            rows = json.loads(z.read(result["manifest"]))["files"]
            assert result["files"] == len(rows) and result["bytes"] == archive.stat().st_size
            assert {"assets/scenes/Valve.redflag", "assets/ui/panel.yaml"} <= set(z.namelist())
            assert "RedflagForge.exe" not in z.namelist()
            for row in rows:
                assert hashlib.sha256(z.read(row["path"])).hexdigest() == row["sha256"]
        assert (await package()).structured_content["archive"] != str(archive)
        assert (await package(include_runtime=True)).is_error
        scene = project / result["scene"]
        original = scene.read_bytes()
        (project / "outside.txt").write_text("outside assets", encoding="utf8")
        scene.write_bytes(b"Scene: Test\nEntities: []\nFile: ../../outside.txt\n")
        assert (await package()).is_error
        scene.write_bytes(original)
        print("PASS stdio discovery, separate ZIPs, referenced HUD, manifest hashes and project boundaries")
        if runtime:
            # 将维修间自己的脚本编译为 Sandbox.dll，解包后直接从游戏入口启动。
            for name in ("mono", "assets/shaders"):
                shutil.copytree(ROOT / "RedflagForge" / name, project / name, dirs_exist_ok=True)
            libs = project / "Resources/Scripts"
            libs.mkdir(parents=True)
            core = libs / "Redflag-ScriptCore.dll"
            shutil.copy2(ROOT / "RedflagForge/Resources/Scripts" / core.name, core)
            compiler = Path(os.environ["WINDIR"]) / "Microsoft.NET/Framework64/v4.0.30319/csc.exe"
            subprocess.run([compiler, "/nologo", "/target:library", f"/out:{libs / 'Sandbox.dll'}",
                            f"/reference:{core}", project / "ValveRoom.cs"], check=True)
            response = await package(include_runtime=True)
            assert not response.is_error, response.content
            result = response.structured_content
            assert result["kind"] == "local_player_debug"
            extracted = project / "extracted"
            with zipfile.ZipFile(result["archive"]) as z:
                assert {"Play.cmd", "scripts/Play-Bundle.ps1", "RedflagForge.exe",
                        "Resources/Scripts/Redflag-ScriptCore.dll", "Resources/Scripts/Sandbox.dll",
                        "mono-2.0-sgen.dll", "shaderc_sharedd.dll", "assets/shaders/DepthViz.glsl"} <= set(z.namelist())
                z.extractall(extracted)
            run = subprocess.run([str(extracted / "RedflagForge.exe"), "--hidden", "--vulkan", "--play",
                                  result["scene"], "--frames", "2"], cwd=extracted, capture_output=True, timeout=120)
            assert run.returncode == 0 and b"Editor: Opened" in run.stdout, run.stdout[-1500:]
            print("PASS extracted Debug player opened the separately compiled workshop game for two Vulkan frames")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", action="store_true", help="验证含本机 Debug 播放器的包")
    with TemporaryDirectory() as temp:
        project = Path(temp).resolve() / "测试 工坊"
        shutil.copytree(ROOT / "examples/valve-room", project)
        server = check_config(project)
        asyncio.run(check_package(project, server, parser.parse_args().runtime))
