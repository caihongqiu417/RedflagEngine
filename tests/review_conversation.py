"""从两局游戏的实录中对照台词与首次出声时间。运行目录传给 check_station_package.py --output。"""
import argparse
import json
from pathlib import Path


def review(folder):
    result = json.loads((folder / "result.json").read_bytes())
    with (folder / "frames.jsonl").open(encoding="utf8") as source:
        questions = [(frame["seconds"], event["text"]) for frame in map(json.loads, source)
                     for event in frame["events"] if event["kind"] == "玩家" and str(event["entity"]) == "1102"]
    media = {}
    gaps = {}
    with (folder / "media.jsonl").open(encoding="utf8") as source:
        for event in map(json.loads, source):
            media.setdefault(event["id"], {})[event["event"]] = event
            if event["event"] == "buffer_gap":
                gaps.setdefault(event["id"], []).append(round(event["gap_ms"]))

    rows = []
    for turn in result["dialogue"]["turns"]:
        asked, question = questions.pop(0)
        assert question == turn["question"]
        audio = media[turn["audio"]["id"]]
        requested = audio["requested"]["seconds"]
        first = audio["first_voice"]["seconds"]
        rows.append({"question": question, "reply": turn["reply"],
                     "input_to_speech_request_s": round(requested - asked, 2),
                     "speech_request_to_first_sound_s": round(first - requested, 2),
                     "input_to_first_sound_s": round(first - asked, 2),
                     "played_samples": audio["finished"]["device_samples"],
                     "observed_buffer_gaps_ms": gaps.get(turn["audio"]["id"], []) if "voice_seconds" in turn else None})
    return {"archive_sha256": result["archive_sha256"], "exe_sha256": result["exe_sha256"], "turns": rows}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before", type=Path)
    parser.add_argument("after", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    report = {"before": review(args.before), "after": review(args.after)}
    assert [t["question"] for t in report["before"]["turns"]] == [t["question"] for t in report["after"]["turns"]]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf8")
