"""从仓库根运行。只取通过的动作；被拒绝的 UE 请求另列，以免错误地计入首动作耗时。"""
from pathlib import Path
import json
import statistics

ROOT = Path(__file__).resolve().parents[1] / "output/validation/runtime-ai"


def percentiles(values):
    rows = list(values)
    if len(rows)<2:
        return dict(n=len(rows))
    return dict(n=len(rows), p50_ms=round(statistics.median(rows),2),
                p95_ms=round(statistics.quantiles(rows,n=100,method="inclusive")[94],2))


def trials(filename):
    return json.loads((ROOT/filename).read_text(encoding="utf8"))["trials"]


if __name__=="__main__":
    voice = trials("engine-voice-8k.json")
    cut = trials("webrtc-stop-20.json")
    device = [list(map(float,line.split())) for line in (ROOT/"playback-20.txt").read_text().splitlines()]
    summary = dict(percentile_method="linear interpolation at (n−1)p, milliseconds",
                   voice={key:percentiles(row[key] for row in voice if row[key]>=0) for key in
                          ("transcript_ms","give_ms","inventory_ms","receipt_ms","first_voice_ms","stop_call_ms")},
                   device={key:percentiles(row[i] for row in device) for i,key in enumerate(("first_voice","stop_call"))},
                   webrtc={key:percentiles(row[key] for row in cut) for key in ("ack_ms","last_loud_ms","quiet_ms")})
    for name,success in (("reserve","reserved"),("walk","moved")):
        data = trials("forge-"+name+"-latency.json")
        summary[name] = dict(attempted=len(data),actions=percentiles(r["action_ms"] for r in data if r["status"]==success))
    (ROOT/"latency-summary.json").write_text(json.dumps(summary,indent=2,ensure_ascii=False)+"\n",encoding="utf8")
