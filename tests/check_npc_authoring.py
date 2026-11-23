"""通过 MCP 客户端改角色副本，用引擎读取角色和行为树，检查实际走到的动作。"""
import asyncio
import csv
import json
import os
import shutil
import subprocess
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
        new = await call("create_npc", new_file="guide.npc.yaml", display_name='引路人「乔」')
        assert new["file"] == "guide.npc.yaml" and new["name"] == '引路人「乔」'
        assert new["persona"] and new["voice"] == "" and new["model"] == "" and new["observe_world"]
        assert [(r["candidate"], r["action"], r["nodes"]) for r in new["routes"]] == [("wait", "wait", [1, 2])]
        self_authored = (work / "guide.tree.yaml").read_bytes() + b"# kept by the artist\n"
        (work / "guide.tree.yaml").write_bytes(self_authored)
        assert (await session.call_tool("create_npc", {"new_file":"guide.npc.yaml", "display_name":"重复"})).is_error
        assert (work / "guide.tree.yaml").read_bytes() == self_authored

        # 只有同名角色文件时，刚创建的树也要撤回；原文件可以由人继续编辑。
        (work / "saved.npc.yaml").write_bytes(b"# manual profile")
        assert (await session.call_tool("create_npc", {"new_file":"saved.npc.yaml", "display_name":"重复"})).is_error
        assert (work / "saved.npc.yaml").read_bytes() == b"# manual profile" and not (work / "saved.tree.yaml").exists()
        assert (await session.call_tool("create_npc", {"new_file":"../next.npc.yaml", "display_name":"越界"})).is_error

        # Forge 从同一份临时请求文件接收回执，结果也能立刻从 MCP 打开。
        bridge = work / "forge-request.json"
        profile = work / "vendor.npc.yaml"
        bridge.write_text(json.dumps({"operation":"create", "profile":str(profile), "name":"商人"}), encoding="utf-8")
        subprocess.run([sys.executable, str(MODULE / "forge_authoring.py"), str(bridge)], check=True, timeout=15)
        assert Path(json.loads(bridge.read_bytes())["profile"]) == profile
        assert (await call("inspect_npc", npc=profile.name))["name"] == "商人"
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
        edited_role = await call("update_npc_role", npc=role.name, persona="说话简短，喜欢修理机器。")
        assert edited_role["persona"] == "说话简短，喜欢修理机器。" and edited_role["instructions"] == role_info["instructions"]
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


def check_transitions(work):
    import animation_transitions as transitions
    from npc_assets import ToolError
    states = "States: [{ID: walk, Clip: Walk}, {ID: idle, Clip: Idle}]\n# artist's note\n"
    profile = work / "host.animator.yaml"
    profile.write_text(states, encoding="utf-8")
    a = {"Seconds": .16, "Control": [0, 1]}
    b = {"Seconds": .1, "Control": [.2, .8]}
    transitions.save(profile, "walk", "idle", None, a)
    transitions.save(profile, "idle", "walk", None, b)
    assert set(transitions.inspect(profile)["states"]) == {"walk", "idle"}
    text = profile.read_text(encoding="utf-8")
    profile.write_text(text.replace("artist's note", "changed by artist"), encoding="utf-8")
    transitions.save(profile, "walk", "idle", a, b)
    for source, old, value in (("walk", a, None), ("missing", None, a), ("idle", None, {"Seconds": -1, "Control": [0, 1]})):
        previous = profile.read_bytes()
        try:
            transitions.save(profile, source, "idle", old, value)
            raise AssertionError("expected invalid or stale transition to be rejected")
        except ToolError:
            assert previous == profile.read_bytes()
    transitions.save(profile, "walk", "idle", b, None)
    assert transitions.inspect(profile)["transitions"] == [dict(From="idle", To="walk", **b)]
    transitions.save(profile, "idle", "walk", b, None)
    assert not transitions.inspect(profile)["transitions"] and "changed by artist" in profile.read_text(encoding="utf-8")

    for sequence in ("[{From: walk, To: idle, Seconds: 0.16}]", "\n  - From: walk\n    To: idle\n    Seconds: 0.16"):
        profile.write_text(states + "Transitions: " + sequence + "\n# Keep this note\n", encoding="utf-8")
        transitions.save(profile, "walk", "idle", a, b)
        assert transitions.inspect(profile)["transitions"][0]["Control"] == b["Control"]
        transitions.save(profile, "walk", "idle", b, None)
        assert not transitions.inspect(profile)["transitions"] and "# Keep this note" in profile.read_text(encoding="utf-8")
    print("PASS: animation transition insert/edit/delete, stale save, native validation, flow/block YAML, comments")


async def check_montages(work):
    project = work / "station"
    assets = project / "assets/ai"
    assets.mkdir(parents=True)
    source = ROOT / "RedflagForge/assets/ai"
    for name in ("watt.npc.yaml", "watt.animator.yaml", "watt.tree.yaml"):
        shutil.copy2(source/name, assets/name)
    shutil.copytree(source/"montages", assets/"montages")
    server = StdioServerParameters(command=sys.executable, args=[str(MODULE/"server.py")],
                                   env=dict(os.environ, REDFLAG_PROJECT=str(project)))
    async with stdio_client(server) as streams, ClientSession(*streams) as client:
        await client.initialize()

        async def call(tool, **params):
            result = await client.call_tool(tool, params)
            assert not result.is_error, result.content
            return result.structured_content

        role = await call("inspect_npc", npc="watt.npc.yaml")
        expected = {"fire","pickup","give","heal","punch","land"}
        assert {r["ID"] for r in role["states"] if r.get("Montage")} == expected
        landing = next(r for r in role["states"] if r["ID"] == "land")
        assert landing["Slot"] == "FullBody" and landing["Montage"] == "montages/land.montage.yaml"
        name = "ai/"+landing["Montage"]
        path = project/"assets"/name
        original = path.read_bytes()
        doc = await call("inspect_animation_montage", montage=name)
        end = next(r["End"] for r in doc["Events"] if r["Name"] == "Recovery")
        path.write_bytes(original + b"# handwritten between read and save\n")
        params = dict(montage=name, event="Recovery", field="End", original=end, seconds=.30)
        assert (await call("set_montage_event_time", **params))["seconds"] == .30
        changed = path.read_bytes()
        assert changed == (original + b"# handwritten between read and save\n").replace(b"End: 0.20833333", b"End: 0.3")
        for invalid in (dict(params, seconds=.29), dict(params, original=.3, seconds=9),
                        dict(params, event="KneeBend"), dict(params, field="Seconds")):
            assert (await client.call_tool("set_montage_event_time", invalid)).is_error and path.read_bytes() == changed
        await call("set_montage_event_time", **dict(params, original=.30, seconds=end))
        assert path.read_bytes() == original + b"# handwritten between read and save\n"
    print("PASS: six shipped montage states, MCP native read/edit, hand-written comments, conflict, invalid time, undo")


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
        check_transitions(work)
        asyncio.run(check_montages(work))
