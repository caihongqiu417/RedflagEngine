"""用两张 3DGS 房间图检查本机视觉模型能否分清红箱子、地台和已经移走的箱子。"""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import sys
import tempfile
from urllib.parse import urlsplit

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from npc_vision import candidate


def run(index_file, model, endpoint, images):
    index = json.loads(index_file.read_bytes())
    image_hashes = {}
    for view in index["views"]:
        image = images / (view["camera"] + ".png") if images else index_file.parent / view["image"]
        view["image"] = str(image.resolve(strict=True))
        image_hashes[image.name] = hashlib.sha256(image.read_bytes()).hexdigest()

    questions = [
        ("usual", "红箱子是哪一个？", "103"),
        ("usual", "灰色地台是哪一个？", "102"),
        ("usual", "Which visible object is the red crate?", "103"),
        ("usual", "Which visible object is the wide gray platform?", "102"),
        ("reversed", "红箱子是哪一个？", "103"),
        ("reversed", "灰色地台是哪一个？", "102"),
        ("absent", "Is there a red crate clearly visible in these pictures? If absent, reply none.", None),
        ("absent", "照片上能看到红箱子吗？如果箱子不在照片里请回答 none。", None),
    ]
    results = []
    with tempfile.TemporaryDirectory() as temp:
        folder = Path(temp)
        for variant in ("usual", "reversed", "absent"):
            current = copy.deepcopy(index)
            for view in current["views"]:
                if variant == "reversed":
                    view["entities"] = dict(reversed(view["entities"].items()))
                elif variant == "absent":
                    # 箱子在图的上半部。保留下方地台和原索引，检查模型会不会只按名字返回箱子。
                    with Image.open(view["image"]) as source:
                        image = source.convert("RGB")
                    ImageDraw.Draw(image).rectangle((0, 0, image.width - 1, image.height // 2), fill="black")
                    target = folder / (view["camera"] + ".png")
                    image.save(target)
                    view["image"] = str(target)
            selected = folder / (variant + ".json")
            selected.write_text(json.dumps(current, ensure_ascii=False), encoding="utf-8")
            for mode, question, expected in questions:
                if mode == variant:
                    actual = candidate(selected, current["scene"], question, endpoint, model)
                    results.append({"case": mode, "question": question, "expected": expected, "actual": actual})
    return {"model": model, "original_image_sha256": image_hashes, "results": results,
            "correct": sum(row["actual"] == row["expected"] for row in results)}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model", required=True)
    parser.add_argument("--endpoint", default="http://127.0.0.1:8080/v1/chat/completions")
    parser.add_argument("--index", type=Path, default=ROOT / "output/npc-vision/index.json")
    parser.add_argument("--images", type=Path, help="另一次烘焙的 val-00.png 和 val-01.png 所在目录")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    if urlsplit(args.endpoint).hostname not in {"127.0.0.1", "::1", "localhost"}:
        parser.error("请使用本机视觉模型的回环地址。")
    result = run(args.index, args.model, args.endpoint, args.images)
    if args.report:
        args.report.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"{result['model']}: {result['correct']} / {len(result['results'])}")
    sys.exit(result["correct"] != len(result["results"]))
