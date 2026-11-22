"""用本地视觉模型玩两次维修间。只在项目副本中引入故障，保留真实的前后两局供 Forge 打开。"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from tempfile import mkdtemp
from threading import Thread

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"scripts/redflag_mcp"))
from asset_changes import apply, propose
from playtest import read_json, write_json
from playtest_metrics import load_profile
from playtest_workflow import editable_fields, iterate


def prepare(exe):
    work = Path(mkdtemp(prefix="harness-valve-",dir=ROOT/"tmp"))
    shutil.copytree(ROOT/"examples/valve-room",work,dirs_exist_ok=True)
    for name in ("mono","assets/shaders"):
        subprocess.run(["cmd","/c","mklink","/J",work/name,ROOT/"RedflagForge"/name],check=True,capture_output=True)
    libs = work/"Resources/Scripts"
    libs.mkdir(parents=True)
    core = libs/"Redflag-ScriptCore.dll"
    shutil.copy2(ROOT/"RedflagForge/Resources/Scripts"/core.name,core)
    compiler = Path(os.environ["WINDIR"])/"Microsoft.NET/Framework64/v4.0.30319/csc.exe"
    subprocess.run([compiler,"/nologo","/target:library","/out:"+str(libs/"Sandbox.dll"),"/reference:"+str(core),work/"ValveRoom.cs"],check=True)
    binary = work/"bin"/"RedflagForge.exe"
    binary.parent.mkdir()
    shutil.copy2(exe,binary)
    for dll in (ROOT/"bin/Debug-windows-x86_64/RedflagForge").glob("*.dll"):
        os.link(dll,binary.parent/dll.name)
    return work,binary


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("--exe",type=Path,default=ROOT/"bin/Debug-windows-x86_64/RedflagForge/RedflagForge.exe")
    cli.add_argument("--setup",action="store_true",help="只创建可在正式 Forge 中打开的项目副本")
    cli.add_argument("--endpoint",default="http://127.0.0.1:8716")
    cli.add_argument("--provider",help="在独立端口启动当前源码的网关，传入本机真实模型的 chat/completions 地址")
    args = cli.parse_args()
    project,binary = prepare(args.exe)
    print(project,flush=True)
    profile = "valve.json"
    fields = editable_fields(project,load_profile(project,profile))
    faulty = [{"asset":v["asset"],"field":v["field"],"before":v["current"],"after":0.0 if k=="walk_speed" else 12}
              for k,v in fields.items()]
    apply(project,propose(project,"复现行走速度为零、提示文字太小的两个问题",faulty)["id"])
    if args.setup:
        return

    intent = "先按 W 走近橙色阀门，然后按 E 打开。若操作员按 W 没移动，把行走速度恢复为 2 米每秒，提示字号调至 26 像素，再进游戏验证。"
    gateway = None
    if args.provider:
        sys.path.insert(0,str(ROOT/"scripts"))
        from decision_service import ChatModel, Server
        gateway = Server(ChatModel(args.provider,"Qwen3-VL-2B-Instruct",True),0)
        Thread(target=gateway.serve_forever,daemon=True).start()
        args.endpoint = f"http://127.0.0.1:{gateway.server_port}"
    try:
        result = iterate(project,"assets/scenes/Valve.redflag",intent,profile,rounds=4,revisions=1,endpoint=args.endpoint,executable=binary)
    finally:
        if gateway:
            gateway.shutdown()
            gateway.server_close()
    first,last = result["attempts"]
    assert any(i["kind"]=="no_progress" for i in first["findings"]),first
    assert last["verdict"]=="met_checks" and last["events"]["valve_open"]==1,last
    assert len(result["changes"])==1,result
    changed = read_json(project/"captures/changes"/result["changes"][0]/"change.json")
    assert len({op["asset"] for op in changed["operations"]})==2,changed

    # 人在另一处加的说明要一直留在原位，包括撤销和重新应用以后。
    layout = project/"assets/ui/panel.yaml"
    with layout.open("a",encoding="utf8") as file:
        file.write("\n# 关卡作者补写的说明\n")
    apply(project,changed["id"],undo=True)
    assert "Font: 12" in layout.read_text("utf8")
    apply(project,changed["id"])
    assert layout.read_text("utf8").endswith("# 关卡作者补写的说明\n")
    out = ROOT/"output/validation/harness-valve"
    out.mkdir(parents=True,exist_ok=True)
    write_json(out/"iterations.json",result)
    write_json(out/"asset-change.json",changed)
    for label,session in zip(("before","after"),result["sessions"]):
        source = project/"captures/playtests"/session
        archive = out/label
        archive.mkdir(exist_ok=True)
        for name in ("session.json","play-goal.json","assessment.json","decisions.jsonl","model-inputs.jsonl","media.jsonl","frames.jsonl","handoff.json","status.json"):
            shutil.copy2(source/name,archive/name)
        for name in ("images","commands","receipts"):
            shutil.copytree(source/name,archive/name,dirs_exist_ok=True)
    print(json.dumps(result,ensure_ascii=False),flush=True)


if __name__ == "__main__":
    main()
