"""按原生试玩记录还原任务的时间与依赖，供外部 Agent 与 Forge 对照。"""
import json


def snapshot(folder, seq):
    if seq < 0:
        raise ValueError("帧号不能为负。")
    file = folder / "jobs.jsonl"
    if not file.is_file():
        return {"requested_seq": seq, "sample_seq": None, "jobs": []}

    latest = None
    jobs = {}
    with file.open("rb") as rows:
        for line in rows:
            if not line.endswith(b"\n"):
                break
            row = json.loads(line)
            if row["seq"] > seq:
                break
            jobs[row["run"], row["id"]] = row
            latest = row["seq"]
    if latest is None:
        return {"requested_seq": seq, "sample_seq": None, "jobs": []}

    batch = [row for row in jobs.values() if row["seq"] == latest]
    times = {}
    result = []
    # 提交时只允许依赖已经存在的任务，所以同一局中依赖的编号一定小于当前编号。
    for key, row in sorted(jobs.items()):
        parents = [(row["run"], parent) for parent in row["depends"]]
        known = [parent for parent in parents if parent in times]
        previous = max(known, key=lambda parent: times[parent][:2], default=None)
        work = row["end_us"] - row["start_us"]
        earlier = times[previous][0] if previous else 0
        # 计时取整到微秒。两条路径耗时相同时，保留包含最后一次写回的完整依赖路径。
        depth = times[previous][1] + 1 if previous else 1
        times[key] = earlier + work, depth, previous
        if row["seq"] == latest:
            # 先前任务可能在本次开始记录前就已结束。这种情况下不能把全部排队时间当作调度开销。
            ready = max([row["queued_us"]] + [jobs[p]["end_us"] for p in known])
            result.append({**row, "work_us": work, "ready_wait_us": row["start_us"]-ready if len(known)==len(parents) else None,
                           "missing_dependencies": [p[1] for p in parents if p not in jobs]})
    last = max(batch, key=lambda row: times[row["run"], row["id"]][:2])
    key = last["run"], last["id"]
    work_us = times[key][0]
    chain = []
    while key is not None:
        chain.append(key[1])
        key = times[key][2]
    return {"requested_seq": seq, "sample_seq": latest, "units": "microseconds", "jobs": result,
            "observed_span_us": max(r["end_us"] for r in batch) - min(r["queued_us"] for r in batch),
            "longest_recorded_work_chain": {"run": last["run"], "ids": chain[::-1], "work_us": work_us}}
