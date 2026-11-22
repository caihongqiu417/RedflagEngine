"""汇总模型实际返回的 usage，把请求、回执、补丁和原生测试保存在同一份压缩包里。"""

import argparse
import csv
import json
import re
import zipfile
from pathlib import Path


def read_json(path):
    return json.loads(path.read_text(encoding="utf8"))


def summarize(root):
    rows = []
    files = set()
    for first in sorted(root.rglob("01-response.json")):
        folder = first.parent
        match = re.fullmatch(r"(trace|pump)-(classic|context)-(\d+)(.*)", folder.name)
        if not match:
            continue

        task, mode, series, protocol = match.groups()
        summary_file = folder / "summary.json"
        summary = read_json(summary_file) if summary_file.exists() else {}
        responses = [read_json(p) for p in sorted(folder.glob("*-response.json"))]
        usage = [r["usage"] for r in responses if "usage" in r]
        inputs = sum(u["prompt_tokens"] for u in usage)
        outputs = sum(u["completion_tokens"] for u in usage)
        if summary and (inputs, outputs) != (summary["input_tokens"], summary["output_tokens"]):
            raise ValueError(f"逐轮计数与运行结束时的统计不符：{folder}")

        # 中途退出仍会消耗 token；原始回执都计入，缺失的 usage 单独记。
        status = "failed" if summary else "interrupted"
        if summary.get("ok"):
            status = "passed"
        if "preflight" in folder.parts:
            status = "preflight"
        rows.append(
            {
                "run": folder.relative_to(root).as_posix(),
                "model": responses[0].get("model", ""),
                "task": task,
                "mode": mode,
                "series": series,
                "workflow": protocol.strip("-") or "free",
                "status": status,
                "calls": len(responses),
                "prompt": inputs,
                "completion": outputs,
                "total": inputs + outputs,
                "cached": sum(u.get("prompt_tokens_details", {}).get("cached_tokens", 0) for u in usage),
                "unmetered": len(responses) - len(usage),
                "tree": summary.get("tree", ""),
            }
        )
        files.update(f for f in folder.iterdir() if f.is_file())

    if not rows:
        raise ValueError(f"{root} 中没有逐轮模型回执")
    with (root / "runs.csv").open("w", newline="", encoding="utf8") as file:
        writer = csv.DictWriter(file, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)

    files.update(root.rglob("model-config.json"))
    with zipfile.ZipFile(
        root / "transcripts.zip", "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9
    ) as archive:
        for path in sorted(files):
            archive.write(path, path.relative_to(root).as_posix())
    passed = sum(r["status"] == "passed" for r in rows)
    print(f"{len(rows)} runs, {sum(r['calls'] for r in rows)} responses, {passed} verified changes")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("folder", type=Path)
    summarize(parser.parse_args().folder.resolve())
