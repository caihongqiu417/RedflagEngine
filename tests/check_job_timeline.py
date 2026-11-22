"""试玩框架以整微秒记录 CPU 任务；即使最后一次写回很短，也应能沿依赖找到它。"""
import json
from pathlib import Path
import sys
from tempfile import TemporaryDirectory

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/"scripts/redflag_mcp"))
from job_timeline import snapshot


with TemporaryDirectory() as temp:
    folder = Path(temp)
    rows = [
        dict(seq=2,run=1,id=1,depends=[],queued_us=1,start_us=2,end_us=7),
        dict(seq=2,run=1,id=2,depends=[1],queued_us=2,start_us=8,end_us=8),
        dict(seq=6,run=2,id=2,depends=[1],queued_us=10,start_us=11,end_us=15),
    ]
    (folder/"jobs.jsonl").write_bytes(("\n".join(json.dumps(r) for r in rows)+"\n{").encode())
    assert snapshot(folder,1)["sample_seq"] is None
    first = snapshot(folder,4)
    assert first["sample_seq"] == 2 and first["jobs"][-1]["ready_wait_us"] == 1
    assert first["longest_recorded_work_chain"]["ids"] == [1,2], first["longest_recorded_work_chain"]
    later = snapshot(folder,8)
    assert later["jobs"][0]["ready_wait_us"] is None and later["jobs"][0]["missing_dependencies"] == [1]
print("时间线保留零微秒写回、跨局缺失依赖，并忽略仍在写入的末行。")
