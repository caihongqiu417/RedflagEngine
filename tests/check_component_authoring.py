"""在临时项目通过 MCP 读维修站角色、预览改动并撤销；原游戏场景保持原样。"""
import asyncio
import os
from pathlib import Path
import shutil
import sys
from tempfile import TemporaryDirectory

from mcp import ClientSession, StdioServerParameters, stdio_client

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "scripts/redflag_mcp"
sys.path.insert(0, str(TOOLS))
import asset_changes


async def check(project):
    scenes = project / "assets/scenes"
    scenes.mkdir(parents=True)
    for name in ("Watt-Station.redflag", "Watt-Station.tuning.yaml"):
        shutil.copyfile(ROOT / "RedflagForge/assets/scenes" / name, scenes / name)
    path = scenes / "Watt-Station.redflag"
    original = path.read_bytes()
    args = {"scene": path.name, "entity": "1101", "component": "CapsuleCollider3DComponent"}
    params = StdioServerParameters(command=sys.executable, args=[str(TOOLS / "server.py")],
                                   env={**os.environ, "REDFLAG_PROJECT": str(project)})
    async with stdio_client(params) as streams, ClientSession(*streams) as client:
        await client.initialize()

        async def call(name, **kwargs):
            result = await client.call_tool(name, kwargs)
            assert not result.is_error, result.content
            return result.structured_content

        data = await call("inspect_scene_fields", **args, proposed={"Radius": 1, "Height": 2.1})
        fields = {f["name"]: f for f in data["fields"]}
        assert data["scope"] == "saved_scene" and fields["Height"]["value"] == 1.84
        assert fields["Height"]["default"] == 1.8 and fields["Height"]["proposed"] == 2.1
        assert "0.9" in fields["Radius"]["tuning"]["reason"] and "reason" not in data["operations"][0]

        ops = data["operations"]
        ops[0].update(reason="测试较宽的角色能否穿过检修通道", evidence=["output/validation/packaged-watt/ground-contact/radius.json"])
        change = await call("propose_asset_change", intent="同时调整角色半径和总高", operations=ops)
        assert path.read_bytes() == original and change["operations"][0]["reason"] == ops[0]["reason"]
        manual = original.replace("Tag: 玩家".encode(), "Tag: 驾驶员".encode(), 1)
        path.write_bytes(manual)
        await call("apply_asset_change", change=change["id"])
        assert b"Radius: 1," in path.read_bytes() and "Tag: 驾驶员".encode() in path.read_bytes()
        await call("apply_asset_change", change=change["id"], undo=True)
        assert path.read_bytes() == manual

        # 提案之后作者改变了总长，新的半径现在装不下。应用要重新检查两个字段的组合。
        wider = await call("inspect_scene_fields", **args, proposed={"Radius": .4})
        guarded = await call("propose_asset_change", intent="加宽", operations=wider["operations"])
        narrow = manual.replace(b"Height: 1.84", b"Height: 0.72", 1)
        path.write_bytes(narrow)
        assert (await client.call_tool("apply_asset_change", {"change": guarded["id"]})).is_error
        assert path.read_bytes() == narrow
        path.write_bytes(manual)

        # 只改半径会令直径超过总长。旧提案保存期间遇到手工改值时，也须保留磁盘内容。
        assert (await client.call_tool("propose_asset_change", {"intent": "测试直径", "operations": [ops[0]]})).is_error
        edited = manual.replace(b"Radius: 0.35", b"Radius: 0.41", 1)
        path.write_bytes(edited)
        assert (await client.call_tool("apply_asset_change", {"change": change["id"]})).is_error
        assert path.read_bytes() == edited
        for wrong in (True, "0.4", -1, 1e100):
            assert (await client.call_tool("inspect_scene_fields", {**args, "proposed": {"Radius": wrong}})).is_error
        assert (await client.call_tool("inspect_scene_fields", {**args, "proposed": {"Axis": "W"}})).is_error
        box = {**args, "entity": "1103", "component": "BoxCollider3DComponent"}
        assert (await client.call_tool("inspect_scene_fields", {**box, "proposed": {"HalfSize": [1, 0, 1]}})).is_error
        rigid = await call("inspect_scene_fields", **{**args, "component": "RigidBody3DComponent"})
        assert next(f for f in rigid["fields"] if f["name"] == "BodyType")["value"] == "Kinematic"

        script = {**args, "component": "ScriptComponent"}
        result = await call("inspect_scene_fields", **script, proposed={"CrouchHeight": 1.34, "DownCenter": [0, .33, -.2]})
        managed = {f["name"]: f for f in result["fields"]}
        assert managed["CrouchHeight"]["value"] == 1.33 and managed["DownRadius"]["value"] == .33
        assert "1.33088" in managed["CrouchHeight"]["tuning"]["reason"]
        change = await call("propose_asset_change", intent="测试脚本高度", operations=result["operations"])
        await call("apply_asset_change", change=change["id"])
        await call("apply_asset_change", change=change["id"], undo=True)
        assert path.read_bytes() == edited
        path.write_bytes(edited.replace(b"Sandbox.StationPlayer", b"Sandbox.Other"))
        assert (await client.call_tool("apply_asset_change", {"change": change["id"]})).is_error
        assert all("tuning" not in f for f in (await call("inspect_scene_fields", **script))["fields"])

        # 两种旧场景写法均省略了 Axis；撤销要重新省略该键，还要保留同时编辑的备注。
        for shape in ("{Radius: 0.3, Height: 1.5}", "\n      Radius: 0.3\n      Height: 1.5"):
            saved = "Scene: Test\nEntities:\n  - Entity: 1101\n    CapsuleCollider3DComponent: " + shape + "\n"
            old = scenes / "Old.redflag"
            old.write_bytes(saved.encode())
            result = await call("inspect_scene_fields", **{**args, "scene": old.name}, proposed={"Axis": "Z", "Center": [1, 2, 3]})
            axis = next(f for f in result["fields"] if f["name"] == "Axis")
            assert axis["value"] == "Y" and axis["source"] == "native_default"
            change = await call("propose_asset_change", intent="测试省略字段", operations=result["operations"])
            await call("apply_asset_change", change=change["id"])
            old.write_bytes(old.read_bytes().replace(b"Scene: Test", b"Scene: Test # manual"))
            await call("apply_asset_change", change=change["id"], undo=True)
            assert old.read_bytes() == saved.replace("Scene: Test", "Scene: Test # manual").encode()

        shared = scenes / "Shared.redflag"
        shared.write_bytes(b"Scene: Shared\nEntities:\n  - Entity: 1\n    BoxCollider3DComponent: &same {HalfSize: [1, 1, 1]}\n"
                           b"  - Entity: 2\n    BoxCollider3DComponent: *same\n")
        result = await call("inspect_scene_fields", **{**box, "entity": "2", "scene": shared.name},
                            proposed={"HalfSize": [1, 2, 3]})
        assert (await client.call_tool("propose_asset_change", {"intent": "只改第二个盒子", "operations": result["operations"]})).is_error
        assert b"[1, 1, 1]" in shared.read_bytes()

    # 非有限数无法通过 JSON 传输，在 Forge 和 MCP 共用的事务入口直接验证。
    for bad in (float("nan"), float("inf")):
        try:
            asset_changes.propose(project, "测试非有限数", [{**ops[0], "before": .41, "after": bad}])
        except ValueError:
            continue
        raise AssertionError("拒绝非有限数")


if __name__ == "__main__":
    with TemporaryDirectory() as work:
        asyncio.run(check(Path(work)))
    print("PASS MCP native/managed fields, tuning, multi-field validation, stale changes, author edits, native defaults and undo")
