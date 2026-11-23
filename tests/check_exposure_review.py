"""在项目副本里验证渲染配置的单字段保存、回读和并发编辑。"""
import asyncio
import json
import os
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory
from unittest.mock import patch

from mcp import ClientSession, StdioServerParameters, stdio_client

MODULE = Path(__file__).resolve().parents[1] / "scripts/redflag_mcp"
sys.path.insert(0, str(MODULE))
import forge_authoring
import render_settings


def exercise(project):
    profile = project / "assets/scenes/yard.redflag.render.yaml"
    profile.parent.mkdir(parents=True)
    (profile.parent / "yard.redflag").write_text("Scene: Yard\nEntities: []\n", encoding="utf8")
    request = {"operation": "exposure", "profile": str(profile)}

    def apply(before, after):
        return forge_authoring.execute(dict(request, original=before, value=after))

    def reports():
        return list((project / "captures/changes/exposure").glob("*.json"))

    # 保留 BOM、CRLF、开发者添加的字段和曝光旁的注释。C++ 界面保存的是单精度原值。
    source = b'\xef\xbb\xbfToneMapping: ACES\r\nExposureEV: -0.35  # artist\r\nCustom: "keep"\r\n'
    profile.write_bytes(source)
    reply = apply(-.349999994, .5)
    record = json.loads(Path(reply["record"]).read_bytes())
    assert "warning" not in reply and record["id"] == reply["id"]
    assert record["assessment"] == {"verdict": "met_checks", "findings": [], "readback_ev": .5}
    assert profile.read_bytes() == source.replace(b"-0.35", b"0.5")
    assert apply(-.35, .5) == {"profile": str(profile)}

    # 两个 Forge 命令同时提交同一份旧值，只有一个能实际写入。
    replies = [project / "first.json", project / "second.json"]
    for file, value in zip(replies, (1, 2)):
        file.write_text(json.dumps(dict(request, original=.5, value=value)), encoding="utf8")
    workers = [subprocess.Popen([sys.executable, str(MODULE / "forge_authoring.py"), str(file)]) for file in replies]
    assert all(worker.wait(timeout=30) == 0 for worker in workers)
    results = [json.loads(file.read_bytes()) for file in replies]
    assert sum("error" in reply for reply in results) == 1 and len(reports()) == 2
    value = 2 if "error" in results[0] else 1
    assert profile.read_bytes() == source.replace(b"-0.35", f"{value}.0".encode())

    for source in (b"# artist\r\nTAA: true\r\n", b"{TAA: true, Mode: Lit}\n", b"{} # keep\n"):
        profile.write_bytes(source)
        assert apply(0, 0) == {"profile": str(profile)} and profile.read_bytes() == source
        result = apply(0, -1.5)
        assert json.loads(Path(result["record"]).read_bytes())["before_absent"]
        assert b"ExposureEV: -1.5" in profile.read_bytes()
        assert render_settings.read_exposure(profile.read_text("utf8"))[2] == -1.5

    count = len(reports())
    for source in (b"", b"# only a comment", b"[1, 2]", b"ExposureEV: [1]\n", b"ExposureEV: 0\nExposureEV: 2\n",
                   b"ExposureEV: .nan", b"ExposureEV: nope", b"ExposureEV: 5", b"ExposureEV: &v 0.0\nOther: *v"):
        profile.write_bytes(source)
        try:
            apply(0, 1)
            raise AssertionError("untrusted source was changed")
        except ValueError:
            assert profile.read_bytes() == source
    profile.unlink()
    try:
        apply(0, 1)
        raise AssertionError("missing source was created")
    except ValueError:
        assert not profile.exists()
    assert len(reports()) == count

    # 写入后读回了意外的数值，文件仍视为已保存，Forge 只收到一条提醒。
    profile.write_text("ExposureEV: 0\n", encoding="utf8")
    read = render_settings.read_exposure
    readings = [read("ExposureEV: 0"), read("ExposureEV: 1"), read("ExposureEV: 2")]
    with patch.object(render_settings, "read_exposure", side_effect=readings):
        result = apply(0, 1)
    assert "error" not in result and "warning" in result and profile.read_text("utf8") == "ExposureEV: 1.0\n"
    assert json.loads(Path(result["record"]).read_bytes())["assessment"]["verdict"] == "findings"
    print("PASS exposure apply, readback, two writers, exact comments/line endings, absent values and invalid sources")


def check_external_scene(directory):
    scene = directory / "独立车间.redflag"
    config = scene.with_name(scene.name + ".render.yaml")
    original = b"ExposureEV: 0 # keep\n"
    config.write_bytes(original)
    request = dict(operation="exposure", profile=str(config), original=0, value=-.5)
    try:
        forge_authoring.execute(request)
        raise AssertionError("orphaned render settings should be rejected")
    except ValueError:
        assert config.read_bytes() == original and not (directory / "captures").exists()

    scene.write_text("Scene: Workshop\nEntities: []\n", encoding="utf8")
    result = forge_authoring.execute(request)
    record = Path(result["record"])
    assert record.parent == directory / "captures/changes/exposure"
    assert json.loads(record.read_bytes())["asset"] == config.name
    assert config.read_bytes() == b"ExposureEV: -0.5 # keep\n"

    other = directory / "lighting.render.yaml"
    other.write_bytes(original)
    try:
        forge_authoring.execute(dict(request, profile=str(other)))
        raise AssertionError("an unrelated YAML must not be edited")
    except ValueError:
        assert other.read_bytes() == original
    return scene


async def check_mcp(project, external):
    scene = project / "assets/scenes/yard.redflag"
    scene.write_text("Scene: Yard\nEntities: []\n", encoding="utf8")
    config = scene.with_name(scene.name + ".render.yaml")
    config.write_text("GTAO: true # keep\n", encoding="utf8")
    server = StdioServerParameters(command=sys.executable, args=[str(MODULE / "server.py")],
                                   env=dict(os.environ, REDFLAG_PROJECT=str(project)))
    async with stdio_client(server) as streams, ClientSession(*streams) as client:
        await client.initialize()
        supported = (await client.call_tool("get_capabilities", {})).structured_content
        assert "set_scene_exposure" in supported["render_settings"]["tools"]
        params = dict(scene=scene.name, original=0, value=.75)
        result = await client.call_tool("set_scene_exposure", params)
        assert not result.is_error and "warning" not in result.structured_content
        record = json.loads(Path(result.structured_content["record"]).read_bytes())
        assert record["before_absent"] and record["assessment"]["readback_ev"] == .75
        assert "GTAO: true # keep" in config.read_text("utf8")

        try:
            forge_authoring.execute(dict(operation="exposure", profile=str(config), original=0, value=1))
            raise AssertionError("stale Forge edit overwrote an MCP change")
        except ValueError:
            settings = await client.call_tool("get_render_settings", {"scene": scene.name})
            assert settings.structured_content["settings"]["ExposureEV"] == .75
        assert (await client.call_tool("set_scene_exposure", dict(params, value=-1))).is_error
        denied = await client.call_tool("set_scene_exposure", dict(scene=str(external), original=-.5, value=3))
        assert denied.is_error and external.with_name(external.name + ".render.yaml").read_bytes() == b"ExposureEV: -0.5 # keep\n"
    print("PASS standalone Forge scene, unrelated files, MCP project scope, shared records and stale writer")


if __name__ == "__main__":
    with TemporaryDirectory() as directory, TemporaryDirectory() as external_dir:
        project = Path(directory).resolve()
        exercise(project)
        external = check_external_scene(Path(external_dir).resolve())
        asyncio.run(check_mcp(project, external))
