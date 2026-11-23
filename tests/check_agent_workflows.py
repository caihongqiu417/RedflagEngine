"""从随仓库提供的 Claude Code 配置连接服务，在临时游戏中读取工作流和一张试玩图。"""
import asyncio
import base64
import json
import os
from pathlib import Path
from tempfile import TemporaryDirectory

from mcp import ClientSession, StdioServerParameters, stdio_client
from PIL import Image


ROOT = Path(__file__).resolve().parents[1]


async def check(project):
    config = json.loads((ROOT / ".mcp.json").read_bytes())["mcpServers"]["redflag"]
    # 从子目录发起连接，和开发者在引擎某个模块中打开 Claude Code 一样。
    server = StdioServerParameters(command=config["command"], args=config["args"], cwd=ROOT / "scripts",
                                   env=dict(os.environ, REDFLAG_PROJECT=str(project)))
    async with stdio_client(server) as streams, ClientSession(*streams) as client:
        handshake = await client.initialize()
        assert "get_workflow" in handshake.instructions
        tools = {tool.name for tool in (await client.list_tools()).tools}

        async def call(tool, **args):
            result = await client.call_tool(tool, args)
            assert not result.is_error, result.content
            return result

        caps = (await call("get_capabilities")).structured_content
        assert Path(caps["project"]) == project and caps["playtest"]["profiles"] == ["test.json"]
        covered = {"get_workflow", "get_capabilities"}
        for topic in caps["workflows"]:
            guide = (await call("get_workflow", topic=topic)).structured_content
            covered.update(guide["tools"])
            for name in guide["skills"]:
                note = (await call("get_workflow", topic=name)).structured_content
                assert note["text"] == (ROOT / note["source"]).read_text(encoding="utf-8")
        assert tools == covered, tools ^ covered
        profile = await call("inspect_playtest_profile", profile="test.json")
        assert profile.structured_content == {"actor": "41"}

        async def image(seq):
            return (await call("read_playtest_image", session="test", seq=seq)).content

        assert [block.type for block in await image(3)] == ["text"]
        for seq, expected in [(5, 4), (19, 4), (20, 20), (None, 20)]:
            info, photo = await image(seq)
            assert json.loads(info.text)["seq"] == expected
            assert photo.type == "image" and photo.mime_type == "image/jpeg"
            assert base64.b64decode(photo.data) == (project / "captures/playtests/test/images" / f"{expected}.jpg").read_bytes()
        result = await client.call_tool("read_playtest_image", {"session": "../test-other"})
        assert result.is_error

        folder = project / "captures/playtests/test"
        (folder / "media.jsonl").write_text('{"seq":30,"image":"../../file.jpg"}\n', encoding="utf-8")
        result = await client.call_tool("read_playtest_image", {"session": "test"})
        assert result.is_error
        print(f"PASS  {len(tools)} tools, {len(caps['workflows'])} workflows, profile and sampled images over stdio")


if __name__ == "__main__":
    with TemporaryDirectory() as temp:
        project = Path(temp).resolve() / "样例 Game"
        profiles = project / "assets/playtests"
        profiles.mkdir(parents=True)
        (profiles / "test.json").write_text('{"actor":"41"}', encoding="utf-8")
        folder = project / "captures/playtests/test"
        (folder / "images").mkdir(parents=True)
        (folder / "session.json").write_text("{}", encoding="utf-8")
        with (folder / "media.jsonl").open("wb") as stream:
            for seq in (4, 20):
                name = f"images/{seq}.jpg"
                Image.new("RGB", (8, 8), (seq, 5, 6)).save(folder / name)
                stream.write(json.dumps({"seq": seq, "seq_end": seq + 1, "image": name}).encode() + b"\n")
            stream.write(b'{"seq":')  # 采集进程还没有写完下一条记录。
        asyncio.run(check(project))
