"""从仓库根目录运行 python tests/check_npc_runtime.py。需要先构建 Debug x64 的 Redflag 和 ScriptCore。"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import threading
from pathlib import Path
from urllib.request import urlopen

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from decision_service import Server


class RepairModel:
    # 测宿主同 Mono、物理和网关的接线。模型替身只从引擎提供的候选里选修理。
    name = "sdk-smoke"
    images = False

    def choose(self, request, cancel):
        return next(item["id"] for item in request["candidates"] if item["id"] == "repair")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, help="复用本机已运行的真实决策服务")
    parser.add_argument("--report", type=Path, help="把通过项、程序集和当前引擎的指纹写入 JSON")
    args = parser.parse_args()
    example = ROOT / "examples/npc-runtime"
    with tempfile.TemporaryDirectory(prefix="redflag-npc-sdk-") as directory:
        work = Path(directory)
        for file in example.glob("*.yaml"):
            shutil.copy2(file, work)
        scripts = work / "Resources/Scripts"
        scripts.mkdir(parents=True)
        shutil.copytree(ROOT / "RedflagForge/mono", work / "mono")
        core = scripts / "Redflag-ScriptCore.dll"
        shutil.copy2(ROOT / "RedflagForge/Resources/Scripts" / core.name, core)
        compiler = Path(os.environ["WINDIR"]) / "Microsoft.NET/Framework64/v4.0.30319/csc.exe"
        subprocess.run([str(compiler), "/nologo", "/target:library", "/reference:" + str(core),
                        "/out:" + str(scripts / "Sandbox.dll"), *map(str, sorted(example.glob("*.cs")))], check=True)

        def run_sample(port):
            return subprocess.run([str(ROOT / "tests/run_scene_tests.cmd"), "--npc-sdk", str(port), str(work)],
                                  cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=150)

        if args.port:
            with urlopen(f"http://127.0.0.1:{args.port}/v1/health", timeout=5) as response:
                model = json.load(response)["model"]
            run = run_sample(args.port)
        else:
            # 实际 HTTP 网关分配一个空闲端口，用户已启动的模型和其他测试可照常使用。
            model = RepairModel.name
            with Server(RepairModel(), 0) as server:
                worker = threading.Thread(target=server.serve_forever, daemon=True)
                worker.start()
                try:
                    run = run_sample(server.server_port)
                finally:
                    server.shutdown()
                    worker.join()
        output = run.stdout.decode("utf8", errors="replace").replace("\r\n", "\n")
        print(output)
        run.check_returncode()
        assert output.count("完成 | repair | 82 | 设备修好了。") == 1, "C# 脚本应只收到一份完成回执"
        assert output.count("Gauge reading 82 = 70") == 1
        assert output.count("Gauge reading cancelled for 82") == 1
        if args.report:
            def digest(file):
                with file.open("rb") as stream:
                    return hashlib.file_digest(stream, "sha256").hexdigest()
            report = dict(model=model, managed_sha256=digest(core),
                          host_sha256=digest(ROOT / "bin-int/Tests/SceneRenderTests.exe"),
                          checks=[row[5:] for row in output.splitlines() if row.startswith("PASS ")],
                          events=[row for row in output.splitlines() if " | " in row])
            args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf8")


if __name__ == "__main__":
    main()
