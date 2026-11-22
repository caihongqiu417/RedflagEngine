"""通过正式 MCP stdio 发布两个独立项目记录，并核对真实证据、失效提示和写入范围。"""
import asyncio
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory

from mcp import ClientSession, StdioServerParameters, stdio_client

ROOT = Path(__file__).resolve().parents[1]
MODULE = ROOT / "scripts/redflag_mcp"
sys.path.insert(0, str(MODULE))
import creation_reviews


async def exercise(project):
    source = ROOT/"output/validation"
    cases = ("animation-events","animated-shadows")
    for name in cases:
        folder = project/"output/validation"/name
        folder.mkdir(parents=True)
        manifest = json.loads((source/name/creation_reviews.MANIFEST).read_text(encoding="utf8"))
        for evidence in manifest["evidence"]:
            shutil.copy2(source/name/evidence["file"], folder/evidence["file"])
    env = dict(os.environ, REDFLAG_PROJECT=str(project))
    args = StdioServerParameters(command=sys.executable,args=[str(MODULE/"server.py")],env=env)
    evidence_count = 0
    async with stdio_client(args) as streams, ClientSession(*streams) as client:
        await client.initialize()
        for name in cases:
            input_file = source/name/creation_reviews.MANIFEST
            original = json.loads(input_file.read_text(encoding="utf8"))
            result = await client.call_tool("publish_creation_review",dict(case=name,description=original))
            assert not result.is_error, result.content
            opened = await client.call_tool("get_creation_review", dict(case=name))
            assert not opened.is_error and original == opened.structured_content, (opened.structured_content,opened.content)
            evidence_count += len(original["evidence"])
        found = await client.call_tool("list_creation_reviews",{})
        assert found.structured_content == {"cases":sorted(cases)}
        folder = project/"output/validation"/cases[0]
        record = folder/creation_reviews.MANIFEST
        original = record.read_bytes()
        altered = json.loads(original)
        for invalid in ("missing.png","../animated-shadows/comparison.json",creation_reviews.MANIFEST):
            altered["evidence"][0]["file"] = invalid
            failed = await client.call_tool("publish_creation_review", dict(case=cases[0],description=altered))
            assert failed.is_error and record.read_bytes()==original
        receipt = folder/"edited-notices.json"
        receipt.write_text("{}\n", encoding="utf8")
        stale = await client.call_tool("get_creation_review",dict(case=cases[0]))
        assert stale.is_error and "edited-notices.json" in str(stale.content)
        corrupt = folder/"invalid.png"
        corrupt.write_text("This file is not a picture.",encoding="utf8")
        altered["evidence"][0]["file"] = corrupt.name
        broken = await client.call_tool("publish_creation_review",dict(case=cases[0],description=altered))
        assert broken.is_error and record.read_bytes()==original

    cli = [sys.executable,str(MODULE/"creation_reviews.py"),"--project",str(project),"check"]
    assert subprocess.run(cli+[cases[0]],capture_output=True).returncode == 1
    assert subprocess.run(cli+[cases[1]],capture_output=True).returncode == 0
    return {"topics":list(cases),"verified_files":evidence_count,"all_three_mcp_tools":True,
            "stale_evidence_detected":True,"rejected_publish_preserved_manifest":True,"cli_checks":True}


if __name__=="__main__":
    with TemporaryDirectory() as scratch:
        report = asyncio.run(exercise(Path(scratch)))
    target = ROOT/"output/validation/creation-review"
    target.mkdir(parents=True,exist_ok=True)
    (target/"protocol.json").write_text(json.dumps(report,indent=2)+"\n",encoding="utf8")
    print(report)
