"""通过本机视觉模型运行完整测试。先用 run_scene_tests.cmd --physics 生成场景测试程序。"""
import argparse
import os
from pathlib import Path
import subprocess
import sys
from threading import Thread

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / "scripts"))
from npc_vision import create_server


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--endpoint", default="http://127.0.0.1:8080/v1/chat/completions")
    parser.add_argument("--model", required=True)
    args = parser.parse_args()
    env = os.environ.copy()
    env["PATH"] = str(root / "bin/Debug-windows-x86_64/RedflagForge") + os.pathsep + env["PATH"]
    with create_server(root / "output/npc-vision/index.json", 0, args.endpoint, args.model) as server:
        worker = Thread(target=server.serve_forever)
        worker.start()
        try:
            subprocess.run([root / "bin-int/Tests/SceneRenderTests.exe", "--npc-vision", str(server.server_port)],
                           cwd=root / "RedflagForge", env=env, check=True, timeout=90)
        finally:
            server.shutdown()
            worker.join()
