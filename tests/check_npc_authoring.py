"""通过 MCP 客户端改角色副本，用引擎读取角色和行为树，检查实际走到的动作。"""
import asyncio
import csv
import shutil
import sys
from pathlib import Path
from tempfile import TemporaryDirectory

from mcp import ClientSession, StdioServerParameters, stdio_client


ROOT = Path(__file__).resolve().parents[1]
MODULE = ROOT / "scripts/redflag_mcp"
START = ("import sys; from pathlib import Path; sys.path.insert(0,sys.argv[1]); import server; "
         "server.npc_assets.ASSETS=Path(sys.argv[2]); server.app.run(transport='stdio')")


async def exercise(work: Path):
    source = work / "watt.npc.yaml"
    tree = work / "watt.tree.yaml"
    original = source.read_bytes(), tree.read_bytes()
    role = work / "医护员.npc.yaml"
    own_tree = work / "医护员.tree.yaml"
    args = StdioServerParameters(command=sys.executable, args=["-c", START, str(MODULE), str(work)])
    async with stdio_client(args) as streams, ClientSession(*streams) as session:
        await session.initialize()

        async def call(tool, **kwargs):
            result = await session.call_tool(tool, kwargs)
            assert not result.is_error, result.content
            return result.structured_content

        assert (await call("get_capabilities"))["npc_tool_exists"]
        assert (await call("list_npcs"))["files"] == ["watt.npc.yaml"]
        copied = await call("copy_npc", npc=source.name, new_file=role.name, display_name="医护员")
        assert copied["name"] == "医护员" and copied["tree"] == own_tree.name
        assert own_tree.read_bytes() == original[1]
        duplicate = await session.call_tool("copy_npc", {"npc": source.name, "new_file": role.name, "display_name": "重复"})
        assert duplicate.is_error

        role_before = role.read_bytes()
        moved = await call("rewire_npc", npc=role.name, source=1, key="reserve", target=5)
        route = next(r for r in moved["routes"] if r["candidate"] == "reserve")
        assert route["action"] == "share" and route["nodes"] == [1, 5]
        old_edge = next(e for e in copied["graph"]["Edges"] if e["From"] == 1 and e.get("Key") == "reserve")
        root_link = f'From: 1, Key: reserve, To: {old_edge["To"]}'.encode()
        expected = original[1].replace(root_link, b"From: 1, Key: reserve, To: 5")
        assert expected != original[1] and own_tree.read_bytes() == expected and role.read_bytes() == role_before
        cycle = await session.call_tool("rewire_npc", {"npc": role.name, "source": 1, "key": "reserve", "target": 1})
        assert cycle.is_error and own_tree.read_bytes() == expected

        changed = await call("set_npc_voice", npc=role.name, voice="zh-CN-XiaoxiaoNeural")
        assert changed["voice"] == "zh-CN-XiaoxiaoNeural"
        old_voice = next(line for line in role_before.splitlines() if line.startswith(b"Voice:"))
        assert role.read_bytes() == role_before.replace(old_voice, b'Voice: "zh-CN-XiaoxiaoNeural"')
        assert own_tree.read_bytes() == expected
        animation = work / "医护员.animator.yaml"
        original_states = animation.read_bytes()
        edited = await call("set_npc_animation", npc=role.name, state="idle", clip="Walking", contact=0.25)
        state = next(s for s in edited["states"] if s["ID"] == "idle")
        assert state["Clip"] == "Walking" and state["Contact"] == 0.25
        changed_states = animation.read_bytes()
        assert b"creator note" in changed_states and changed_states != original_states
        assert (await session.call_tool("set_npc_animation", {"npc": role.name, "state": "idle", "contact": 1.2})).is_error
        assert animation.read_bytes() == changed_states and own_tree.read_bytes() == expected
        role_info = await call("update_npc_role", npc=role.name, instructions="保管仓库。\n先看库存。")
        assert role_info["instructions"] == "保管仓库。\n先看库存。" and role_info["voice"] == changed["voice"]
        assert own_tree.read_bytes() == expected
        assert (await session.call_tool("inspect_npc", {"npc": "../medic.npc.yaml"})).is_error

    # 另开一个服务进程，确保轨迹与音色都来自已经保存的内容。
    async with stdio_client(args) as streams, ClientSession(*streams) as session:
        await session.initialize()
        reopened = (await session.call_tool("inspect_npc", {"npc": role.name})).structured_content
        assert reopened["routes"] == changed["routes"] and reopened["voice"] == changed["voice"]
        assert reopened["name"] == "医护员" and reopened["instructions"] == role_info["instructions"]
    assert (source.read_bytes(), tree.read_bytes()) == original
    print("PASS: copy, rewire reserve -> share, reject cycle, voice-only change, edit instructions, reopen in new process")


def check_tables(work: Path):
    sys.path.insert(0, str(MODULE))
    import npc_assets
    import npc_table
    npc_assets.ASSETS = work
    name = "医护员.npc.yaml"
    tree = work / "医护员.tree.yaml"
    profile = work / name

    def change(table, fields, value):
        with table.open(encoding="utf-8-sig", newline="") as f:
            rows = list(csv.reader(f, dialect="excel-tab"))
        next(row for row in rows[1:] if row[1:-2] == fields)[-1] = value
        with table.open("w", encoding="utf-8-sig", newline="") as f:
            csv.writer(f, dialect="excel-tab").writerows(rows)

    before = tree.read_bytes()
    path = work / "role.tsv"
    npc_table.export(name, "profile", path)
    change(path, ["音色"], "table-voice")
    # 导出表以后有人改了名称，导入音色时应保留最新的名称。
    npc_assets.update(name, {"Name": "护士"})
    readback = npc_table.apply(name, "profile", path)
    assert tree.read_bytes() == before and readback["voice"] == "table-voice" and readback["name"] == "护士"

    before = profile.read_bytes()
    links = work / "links.tsv"
    npc_table.export(name, "links", links)
    change(links, ["1", "reserve"], "12")
    readback = npc_table.apply(name, "links", links)
    assert next(r for r in readback["routes"] if r["candidate"] == "reserve")["action"] == "reserve"
    assert before == profile.read_bytes()

    csv_path = work / "actions.csv"
    npc_table.export(name, "intents", csv_path)
    with csv_path.open(encoding="utf-8-sig", newline="") as f:
        rows = list(csv.reader(f))
    next(row for row in rows if row[1:3] == ["reserve", "动作说明"])[-1] = "在原地等待队友取得药品"
    with csv_path.open("w", encoding="utf-8-sig", newline="") as f: csv.writer(f).writerows(rows)
    readback = npc_table.apply(name, "intents", csv_path)
    assert next(r for r in readback["routes"] if r["candidate"] == "reserve")["description"] == "在原地等待队友取得药品"
    assert sum(x != y for x,y in zip(before.splitlines(), profile.read_bytes().splitlines())) == 1
    print("PASS: TSV voice-only + parallel name edit; TSV rewire; single candidate CSV row; native reopen")


if __name__ == "__main__":
    with TemporaryDirectory() as directory:
        work = Path(directory).resolve()
        for name in ("watt.npc.yaml", "watt.tree.yaml"):
            shutil.copyfile(ROOT / "RedflagForge/assets/ai" / name, work / name)
        import re
        profile = work / "watt.npc.yaml"
        text = re.sub(r"(?m)^Animator:[^\n]*\n?", "", profile.read_text(encoding="utf8"))
        profile.write_text(text.rstrip() + "\nAnimator: watt.animator.yaml\n", encoding="utf8")
        (work / "watt.animator.yaml").write_text(
            "States:\n  - {ID: idle, Clip: Idle, Loop: true, Contact: 0.5, Note: creator note}\n", encoding="utf8")
        asyncio.run(exercise(work))
        check_tables(work)
