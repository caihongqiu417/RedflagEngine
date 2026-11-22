"""通过 MCP 标准输入输出握手，在已保存的场景和真实的 Debug 编辑器上检查工具。"""
import asyncio
import hashlib
from pathlib import Path
import sys
from time import monotonic
from tempfile import NamedTemporaryFile

from mcp import ClientSession, StdioServerParameters, stdio_client


ROOT = Path(__file__).resolve().parents[1]
FORGE = ROOT / "RedflagForge"


async def main():
    server = StdioServerParameters(command=sys.executable, args=[str(ROOT / "scripts/redflag_mcp/server.py")])
    async with stdio_client(server) as streams, ClientSession(*streams, read_timeout_seconds=180) as session:
        handshake = await session.initialize()
        assert handshake.server_info.name == "redflag"
        print("initialize:", handshake.server_info.name, handshake.protocol_version)
        tools = {t.name for t in (await session.list_tools()).tools}
        assert {"get_capabilities", "list_scenes", "inspect_scene", "get_render_settings", "check_scene",
                "list_npcs", "inspect_npc", "copy_npc", "update_npc_role", "set_npc_voice", "rewire_npc"} <= tools
        print("tools/list:", ", ".join(sorted(tools)))

        async def call(name, **args):
            result = await session.call_tool(name, args)
            assert not result.is_error, result.content
            return result.structured_content

        name = "Sponza-Atrium.redflag"
        assert (await call("get_capabilities"))["editor_exists"]
        scenes = (await call("list_scenes"))["scenes"]
        assert name in scenes
        environment = await call("inspect_environment", scene="Watt-Station.redflag")
        zones = {r["name"]: r for r in environment["zones"]}
        assert zones["维修棚工位"]["settings"]["Lights"] == ["1223"]
        assert any(r["id"] == "1223" for r in environment["lights"])
        # 在独立场景补写文字，再模拟作者改过同一项。只对照这一次编辑涉及的字节。
        with NamedTemporaryFile(dir=FORGE / "assets/scenes", suffix=".redflag", delete=False) as stream:
            example = Path(stream.name)
            stream.write(b"Scene: Test\nEntities:\n  - Entity: 91\n    EnvironmentZoneComponent: {HalfSize: [1,1,1]} # keep\n")
        try:
            args = dict(scene=example.name, entity="91", field="Landmark", previous="")
            await call("set_zone_description", **args, text="旧水塔")
            assert 'Landmark: "旧水塔"} # keep' in example.read_text(encoding="utf8")
            assert (await session.call_tool("set_zone_description", {**args, "text": "塔"})).is_error
            after = await call("inspect_environment", scene=example.name)
            assert after["zones"][0]["settings"]["Landmark"] == "旧水塔"
        finally:
            example.unlink(missing_ok=True)
        info = await call("inspect_scene", scene=name, limit=1)
        assert info["sky_visible"] and info["environment"] == "../textures/sky/daylight.hdr"
        assert info["next_offset"] == 1 and len(info["entities"]) == 1
        part2 = await call("inspect_scene", scene=name, offset=1, limit=1)
        assert info["entities"][0]["id"] != part2["entities"][0]["id"]
        full = await call("inspect_scene", scene=name)
        model = next(e["components"]["ModelRendererComponent"] for e in full["entities"]
                     if "ModelRendererComponent" in e["components"])
        assert model["ModelID"] == "14269338671407101124"
        settings = await call("get_render_settings", scene=name)
        assert settings["settings"]["Indirect"] == "DDGI"
        assert settings["settings"]["ToneMapping"] == "ACES" and settings["settings"]["ExposureEV"] == 0
        assert (await call("get_render_settings", scene="New.redflag"))["file"] is None
        for tool in ("inspect_scene", "get_render_settings", "check_scene"):
            assert (await session.call_tool(tool, {"scene": "../README.md"})).is_error

        files = [FORGE / "assets/scenes" / name, FORGE / "assets/scenes" / settings["file"],
                 FORGE / "imgui-vulkan.ini"]
        def snapshot():
            return [hashlib.sha256(file.read_bytes()).digest() if file.exists() else None for file in files]
        before = snapshot()
        started = monotonic()
        check = await call("check_scene", scene=name)
        print(f"check_scene ({monotonic() - started:.2f} s):", check)
        assert check["status"] == "completed" and check["exit_code"] == 0
        assert any("opened " in line.lower() and name in line for line in check["diagnostics"])
        assert before == snapshot()
        print(f"PASS {len(scenes)} scenes; pagination, settings, path checks and isolated editor run")


if __name__ == "__main__":
    asyncio.run(main())
