"""从真实试玩截取带 CPU 任务的帧，在正式 Forge 中打开两帧对照。"""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import shutil
import tempfile

import numpy as np
from PIL import Image

from capture_creation_review import capture
from profile_native_navigation import ROOT

OUT = ROOT / "output/validation/job-timeline"


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", type=Path, default=OUT / "replay")
    parser.add_argument("--exe", type=Path, default=ROOT / "bin/Debug-windows-x86_64/RedflagForge/RedflagForge.exe")
    args = parser.parse_args()
    source = args.session.resolve(strict=True)
    archive = OUT / "replay"
    archive.mkdir(parents=True, exist_ok=True)
    jobs = [json.loads(line) for line in (source / "jobs.jsonl").read_bytes().splitlines()]
    scenes = sorted({job["seq"] for job in jobs})
    assert len(scenes) >= 2, "需要两帧实际记录用于比较"
    if source != archive:
        for name in ("jobs.jsonl", "session.json", "status.json"):
            shutil.copyfile(source / name, archive / name)
        raw = (source / "frames.jsonl").read_bytes().splitlines(keepends=True)
        (archive / "frames.jsonl").write_bytes(b"".join(line for line in raw if json.loads(line)["seq"] in scenes))

    # 保留原句柄和时钟值，绝对微秒转换为图上以首个入队时刻为零的毫秒。
    measurements = []
    for seq in scenes:
        rows = [j for j in jobs if j["seq"] == seq]
        ids = {(j["run"], j["id"]): j for j in rows}
        for j in rows:
            assert j["queued_us"] <= j["start_us"] <= j["end_us"]
            for parent in j["depends"]:
                if (j["run"], parent) in ids:
                    assert ids[(j["run"], parent)]["end_us"] <= j["start_us"]
        begin = min(j["queued_us"] for j in rows)
        measurements.append(dict(seq=seq, count=len(rows), span_ms=(max(j["end_us"] for j in rows)-begin)/1000,
            jobs=[dict(id=j["id"], name=j["name"], queue_ms=(j["start_us"]-j["queued_us"])/1000,
                       execution_ms=(j["end_us"]-j["start_us"])/1000, depends=j["depends"]) for j in rows]))

    ctypes.windll.user32.SetProcessDPIAware()
    folder = Path(tempfile.mkdtemp(prefix="job-review-", dir=ROOT / "tmp"))
    session = json.loads((archive / "session.json").read_bytes())["session"]
    shutil.copytree(archive, folder / "captures/playtests" / session)
    viewer = folder / "RedflagForge.exe"
    shutil.copyfile(args.exe, viewer)
    shot = OUT / "forge.png"
    capture(viewer, session, "jobs", folder, shot,
            {"REDFLAG_REVIEW_FRAME":str(scenes[0]), "REDFLAG_REVIEW_BASELINE":session, "REDFLAG_REVIEW_BASE_FRAME":str(scenes[-1])})

    # 色条由 Forge 原生绘制。整行中的青色像素能检出这四条实测工作线程；黑屏或普通回放页都不能通过。
    rgb = np.asarray(Image.open(shot).convert("RGB")).astype(int)
    bars = np.all(np.abs(rgb - [83, 162, 164]) <= 4, axis=2).sum(axis=1) > 160
    runs = int(np.count_nonzero(bars & ~np.r_[False, bars[:-1]]))
    assert runs >= 4, f"未看到四条工作线程：{runs}"
    data = dict(viewer_sha256=digest(viewer), job_log_sha256=digest(archive/"jobs.jsonl"), worker_rows_in_screenshot=runs,
                frames=measurements, sample_overhead="unmeasured")
    (OUT / "check.json").write_text(json.dumps(data, ensure_ascii=False, indent=2)+"\n",encoding="utf8")
    print(f"PASS Forge CPU timeline: frame {scenes[0]} vs {scenes[-1]}, {runs} worker bars. {shot}")


if __name__ == "__main__":
    main()
