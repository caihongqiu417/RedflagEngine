"""从仓库根用 redflag-mcp 虚拟环境运行。需要先构建 NpcTool，并在本机启动模型服务。"""
import argparse
import asyncio
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from urllib.request import urlopen
from mcp import ClientSession, StdioServerParameters, stdio_client

ROOT = Path(__file__).resolve().parents[1]
NAMES = ("lin.npc.yaml", "shen.npc.yaml")


def actions(work, port, build=False):
    profiles = work / "assets/ai"
    host = ROOT / "bin-int/Tests/NpcRelationshipTests.exe"
    command = [str(ROOT/"tests/run_scene_tests.cmd") if build else str(host), "--npc-relationship", str(port), str(profiles)]
    env = dict(os.environ)
    env["PATH"] = str(ROOT/"bin/Debug-windows-x86_64/RedflagForge") + os.pathsep + env["PATH"]
    run = subprocess.run(command, cwd=ROOT/"RedflagForge", env=env, capture_output=True)
    lines = run.stdout.decode("utf8", errors="replace").splitlines()
    rows = []
    for line in lines:
        if line.startswith("ROLE\t"):
            _, file, sent, intent, first, finished, start, arrived, received = line.split("\t")
            rows.append(dict(file=file, submitted=bool(int(sent)), choice=intent, first_action=first, completed=finished,
                             npc_z_after_choice_m=float(start), npc_z_at_transfer_m=float(arrived),
                             teammate_medkits=int(received)))
    if run.returncode or len(rows)!=3: raise RuntimeError("\n".join(lines[-15:]))
    return rows


async def exercise(work, port):
    params = StdioServerParameters(command=sys.executable, args=[str(ROOT/"scripts/redflag_mcp/server.py")],
                                  env={"REDFLAG_PROJECT":str(work), "PYTHONUTF8":"1"})
    async with stdio_client(params) as streams, ClientSession(*streams) as session:
        await session.initialize()

        async def call(tool, **kwargs):
            answer = await session.call_tool(tool,kwargs)
            if answer.is_error: raise RuntimeError(str(answer.content))
            return answer.structured_content

        profiles = [await call("inspect_npc", npc=name) for name in NAMES]
        agreements = [p["leader_relationship"] for p in profiles]
        assert all(agreements) and agreements[0] != agreements[1]
        assert profiles[0]["instructions"] == profiles[1]["instructions"]
        assert profiles[0]["routes"] == profiles[1]["routes"]
        before = await asyncio.to_thread(actions,work,port,True)
        for name, agreement in zip(NAMES, reversed(agreements)):
            changed = await call("update_npc_role", npc=name,leader_relationship=agreement)
            assert changed["leader_relationship"] == agreement
        after = await asyncio.to_thread(actions,work,port)
        assert [s["choice"] for s in before] == ["reserve", "share", ""]
        assert [s["choice"] for s in after]  == ["share", "reserve", ""]
        assert not (await call("update_npc_role",npc=NAMES[0],leader_relationship=""))["leader_relationship"]
        return dict(prompt="按我们的老规矩处理这份医疗包。", baseline=before, swapped=after,
                    agreements=dict(zip(NAMES,agreements)))


if __name__ == "__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port",type=int,default=8716)
    parser.add_argument("--out",type=Path,default=ROOT/"output/validation/runtime-ai/role-agreements.json")
    args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="npc-agreements-") as folder:
        work=Path(folder)
        assets=work/"assets/ai"
        assets.mkdir(parents=True)
        for name in (*NAMES, "watt.tree.yaml", "watt.animator.yaml"):
            shutil.copy2(ROOT/"RedflagForge/assets/ai"/name,assets/name)
        with urlopen(f"http://127.0.0.1:{args.port}/v1/health",timeout=5) as reply:
            model=json.load(reply)["model"]
        result=asyncio.run(exercise(work,args.port))
    result["model"]=model
    result["host_sha256"]=hashlib.sha256((ROOT/"bin-int/Tests/NpcRelationshipTests.exe").read_bytes()).hexdigest()
    args.out.write_text(json.dumps(result,ensure_ascii=False,indent=2)+"\n",encoding="utf8")
    print(args.out)
