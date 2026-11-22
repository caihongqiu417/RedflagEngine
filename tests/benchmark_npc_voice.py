"""从仓库根运行。将同一段普通话交给真实 ASR、NPC、库存、动画时序和 Windows 音频设备。"""
import argparse
import hashlib
import json
import subprocess
import sys
import threading
from pathlib import Path
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from decision_service import ChatModel, Server


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--endpoint", required=True)
    parser.add_argument("--model", default="Qwen3-VL-2B-Instruct")
    parser.add_argument("--trials", type=int, default=20)
    parser.add_argument("--out", type=Path, required=True)
    opts = parser.parse_args()
    if urlsplit(opts.endpoint).hostname != "127.0.0.1":
        parser.error("此项对照使用本机模型")
    wav = ROOT / "output/validation/runtime-ai/medicine.wav"
    with Server(ChatModel(opts.endpoint, opts.model, False), 0) as server:
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        try:
            run = subprocess.run([str(ROOT / "tests/run_scene_tests.cmd"), "--npc-voice-latency", str(server.server_port),
                                  str(opts.trials), str(wav)], cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        finally:
            server.shutdown()
            worker.join()
    fields = ("transcript_ms", "choice_ms", "give_ms", "inventory_ms", "receipt_ms", "first_voice_ms", "stop_call_ms")
    samples = []
    for row in run.stdout.decode("utf8", errors="replace").splitlines():
        if row.startswith("VOICE\t"):
            _, trial, decision, *values = row.split("\t")
            samples.append(dict(trial=int(trial), decision=decision, **dict(zip(fields, map(float, values), strict=True))))
    report = dict(model=opts.model, wav_sha256=hashlib.sha256(wav.read_bytes()).hexdigest(),
                  host_sha256=hashlib.sha256((ROOT/"bin-int/Tests/NpcVoiceLatency.exe").read_bytes()).hexdigest(),
                  passed=run.returncode==0 and len(samples)==opts.trials, trials=samples)
    opts.out.write_text(json.dumps(report, ensure_ascii=False, indent=2)+"\n", encoding="utf8")
    print(opts.out, "passed" if report["passed"] else "failed")
    if not report["passed"]: sys.exit(1)


if __name__ == "__main__":
    main()
