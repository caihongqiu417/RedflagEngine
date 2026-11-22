"""从仓库根运行 python tests/check_npc_memory.py，需先构建 Debug x64 的引擎和 ScriptCore。"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import threading

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from decision_service import Server, messages
from check_decision_service import call, decision


class Reader:
    name = "memory-contract-test"
    images = False

    def __init__(self, port=None):
        self.calls = []
        self.url = f"http://127.0.0.1:{port}" if port else None
        if self.url:
            self.name = call(self.url + "/v1/health")["model"]

    def choose(self, request, cancel):
        records = {item["id"]: item["description"] for item in request["observations"]}
        result = decision(self.url, request) if self.url else dict(status="completed",
            choice="confirm" if "seen/entity/82" in records else "revisit")
        assert result["status"] == "completed", result
        self.calls.append(dict(records=records, result=result, request=request, prompt=messages(request)[1]["content"]))
        return result["choice"]


def run_reader(model, directory, mode="--npc-memory", script=None):
    example = ROOT / "examples/npc-memory"
    work = Path(directory)
    for asset in example.glob("*.yaml"):
        shutil.copy2(asset, work)
    scripts = work / "Resources/Scripts"
    scripts.mkdir(parents=True)
    shutil.copytree(ROOT / "RedflagForge/mono", work / "mono")
    core = scripts / "Redflag-ScriptCore.dll"
    shutil.copy2(ROOT / "RedflagForge/Resources/Scripts" / core.name, core)
    compiler = Path(os.environ["WINDIR"]) / "Microsoft.NET/Framework64/v4.0.30319/csc.exe"
    subprocess.run([str(compiler), "/nologo", "/target:library", "/reference:" + str(core),
                    "/out:" + str(scripts / "Sandbox.dll"), str(script or example / "SurveyNotes.cs")], check=True)
    with Server(model, 0) as server:
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        try:
            run = subprocess.run([str(ROOT / "tests/run_scene_tests.cmd"), mode, str(server.server_port), str(work)],
                                 stdout=subprocess.PIPE, stderr=subprocess.STDOUT, cwd=ROOT, timeout=120)
        finally:
            server.shutdown()
            worker.join()
    def decode(line):
        try:
            return line.decode("utf8")
        except UnicodeDecodeError:
            return line.decode("mbcs", errors="replace")
    output = "\n".join(decode(line) for line in run.stdout.splitlines())
    print(output)
    run.check_returncode()
    save = work / "surveyor.save.yaml"
    return output, save.read_text(encoding="utf-8-sig") if save.exists() else "", hashlib.sha256(core.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model-port", type=int, help="让本机已启动的模型实际决定宿主里的四次动作")
    args = parser.parse_args()
    model = Reader(args.model_port)
    with tempfile.TemporaryDirectory(prefix="redflag-memory-") as directory:
        output, save, core_hash = run_reader(model, directory)
    assert len(model.calls) == 4
    assert len({turn["request"]["prompt"] for turn in model.calls}) == 1
    assert [turn["result"]["choice"] for turn in model.calls] == ["confirm", "revisit", "revisit", "revisit"]
    assert [len(turn["request"]["candidates"]) for turn in model.calls] == [2, 1, 1, 1]
    turns = dict(zip(("visible", "occluded", "removed", "reopened"), model.calls))
    visible, hidden, removed, reopened = (turn["records"] for turn in model.calls)
    assert "红色阀门" in visible["seen/entity/82"] and "蓝色" in visible["reported/entity/82"]
    assert "round-a.png" in visible["reported/entity/82"] and "reported/image/panel-b.png" in visible
    for old in (hidden, removed):
        assert old["last_seen/entity/82"].split(" | ")[1:] == visible["seen/entity/82"].split(" | ")[1:]
        assert "seen/entity/82" not in old and "last_seen" not in old
        assert old["reported/entity/82"] == visible["reported/entity/82"]
        assert old["preference/route"] == "north" and old["preference/language"] == "zh"
    assert save.strip() == "language: zh"
    assert set(reopened) == {"seen/entity/84", "preference/language"}
    assert "Reopened=True language=zh route=unset" in output and "Unlisted preference accepted=False" in output
    assert "C# Seen 82" in output and "C# LastSeen 82" in output
    report_name = "memory-model.json" if args.model_port else "memory.json"
    report = ROOT / "output/validation/runtime-ai" / report_name
    report.parent.mkdir(parents=True, exist_ok=True)
    print(model.name, [turn["result"]["choice"] for turn in model.calls])
    report.write_text(json.dumps(dict(provider=model.name, managed_sha256=core_hash, turns=turns, save=save,
                                      checks=[s for s in output.splitlines() if s.startswith(("PASS", "C#", "Reopened="))]),
                                 indent=2, ensure_ascii=False) + "\n", encoding="utf8")


if __name__ == "__main__":
    main()
