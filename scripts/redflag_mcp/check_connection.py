"""通过本机 stdio 连接 Redflag MCP，读一个已保存的场景并检查开发工具。"""
import argparse
import asyncio
import os
from pathlib import Path
import subprocess
import sys

from project import ENGINE, PROJECT


PYTHON = ENGINE / "bin-int/redflag-mcp" / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
SERVER = Path(__file__).resolve().with_name("server.py")


async def probe(project):
    from mcp import ClientSession, StdioServerParameters, stdio_client

    server = StdioServerParameters(command=str(PYTHON), args=[str(SERVER)], cwd=ENGINE,
                                   env=dict(os.environ, REDFLAG_PROJECT=str(project)))
    async with stdio_client(server) as streams, ClientSession(*streams, read_timeout_seconds=20) as client:
        handshake = await client.initialize()
        name = handshake.server_info.name
        if name != "redflag":
            raise ValueError(f"连接到了 {name}，预期为 redflag。")
        tools = {t.name for t in (await client.list_tools()).tools}
        print(f"MCP 已连接  {name}  {handshake.protocol_version}  本次列出 {len(tools)} 项工具", flush=True)

        async def call(name, **args):
            if name not in tools:
                raise ValueError(f"服务未公开 {name}。")
            reply = await client.call_tool(name, args)
            if reply.is_error:
                raise ValueError(f"{name} 调用失败。{reply.content[0].text}")
            return reply.structured_content

        caps = await call("get_capabilities")
        if Path(caps["project"]).resolve() != project:
            raise ValueError(f"服务打开了其他项目：{caps['project']}。")
        print("项目  " + str(project))
        print("Forge  " + ("已找到 " if caps["editor_exists"] else "缺少 ") + str(ENGINE / caps["editor"]))

        scenes = (await call("list_scenes"))["scenes"]
        if scenes:
            scene = await call("inspect_scene", scene=scenes[0], limit=1)
            print(f"场景  共 {len(scenes)} 个，已读取 {scene['scene']}（{scene['total_entities']} 个实体）")
        else:
            print("场景  assets/scenes 里没有可读取的 .redflag 场景")
        return caps["editor_exists"] and bool(scenes)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path, default=PROJECT, help="包含 assets 的游戏项目目录")
    parser.add_argument("--require-runtime", action="store_true", help="找不到 Debug Forge 或没有可读取的场景时返回失败")
    args = parser.parse_args()
    project = args.project.resolve()
    if not PYTHON.is_file():
        sys.exit(f"缺少 MCP 虚拟环境 {PYTHON}。请按 scripts/redflag_mcp/README.md 安装依赖。")
    if Path(sys.executable) != PYTHON:
        return subprocess.call([str(PYTHON), str(Path(__file__).resolve()), *sys.argv[1:]])

    try:
        ready = asyncio.run(asyncio.wait_for(probe(project), timeout=45))
    except Exception as error:
        # stdio 客户端会把子进程的错误包在任务组里。给开发者显示具体失败的那一步。
        while isinstance(error, ExceptionGroup):
            error = error.exceptions[0]
        if isinstance(error, TimeoutError):
            sys.exit("MCP 在 45 秒内没有完成连接或读取。")
        sys.exit(f"MCP 检查失败。{error}")

    from project_build import msbuild
    try:
        compiler = msbuild()
        if not compiler.is_file():
            raise ValueError(f"找不到 {compiler}")
        print("MSBuild  " + str(compiler))
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(f"MSBuild 未就绪  {error}")
    return 1 if args.require_runtime and not ready else 0


if __name__ == "__main__":
    sys.exit(main())
