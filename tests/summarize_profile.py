"""汇总 Instrumentor 的 CPU 区间；不把等待时间解释为 GPU 耗时。"""
import collections
import hashlib
import json
import statistics
import sys
from pathlib import Path


path = Path(sys.argv[1])
raw = path.read_bytes()
events = json.loads(raw)["traceEvents"]
groups = collections.defaultdict(list)
for event in events:
    if event["ph"] == "X":
        groups[event["name"]].append(event["dur"])

result = {
    "trace": path.name,
    "sha256": hashlib.sha256(raw).hexdigest(),
    "unit": "microseconds",
    "timing": "CPU elapsed intervals, including driver calls and instrumented logging overhead",
    "event_count": len(events),
    "thread_count": len({event["tid"] for event in events}),
    "groups": {
        name: {"samples": len(values), "median": statistics.median(values),
               "min": min(values), "max": max(values)}
        for name, values in groups.items()
    },
}
print(json.dumps(result, indent=2))
