"""用 Qwen 检查照片中的台面区域、移走台面后的空图以及实际返回的世界方向。"""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import tempfile
from unittest.mock import patch
from urllib.parse import urlsplit

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from npc_vision import find_candidate


def measure(index_path, endpoint, model):
    index = json.loads(index_path.read_bytes())
    scene = index["scene"]
    questions = ("请指出地台右侧的灰色台面。", "Find the right side of the gray platform.")
    results = {}
    hashes = {v["camera"]: hashlib.sha256((index_path.parent / v["image"]).read_bytes()).hexdigest()
              for v in index["views"]}
    with tempfile.TemporaryDirectory() as temp:
        folder = Path(temp)
        masked = folder / "index.json"
        for view in index["views"]:
            img = Image.open(index_path.parent / view["image"]).convert("RGB")
            # 遮去画面下方的台面，保留上部箱子和原有的文字标注。
            ImageDraw.Draw(img).rectangle((0, 65, img.width - 1, img.height - 1), fill="black")
            img.save(folder / (view["camera"] + ".png"))
            view["image"] = view["camera"] + ".png"
        masked.write_text(json.dumps(index, ensure_ascii=False), encoding="utf-8")
        for case, source in (("present", index_path), ("removed", masked)):
            for q in questions:
                actual = find_candidate(source, scene, q, endpoint, model)
                results.setdefault(case, []).append(dict(question=q, answer=actual))

        # 这张照片属于同一间房，但本次没选用。它登记的实体不能混进返回结果。
        index["views"].append(dict(camera="back-door", tags=["后门"], image="not-selected.png",
                                   entities={"500": "后门把手"}))
        masked.write_text(json.dumps(index, ensure_ascii=False), encoding="utf-8")
        with patch("npc_vision.ask_model", return_value="500"):
            assert find_candidate(masked, scene, questions[0], endpoint, model) == {"entity": None}

    # 模型不能通过自报机位、编造编号或坐标来绕开本次发送的图片。
    for answer in ("R9", "val-01/right-surface", "104: 另一块台面", "99999999", "1, 2, 3"):
        with patch("npc_vision.ask_model", return_value=answer):
            assert find_candidate(index_path, scene, questions[0], endpoint, model) == {"entity": None}

    accepted = sum(r["answer"].get("region", {}).get("id") == "right-surface" and r["answer"]["entity"] is None
                   for r in results["present"])
    rejected = sum(r["answer"] == {"entity": None} for r in results["removed"])
    mixed = ("右边那个红箱子是哪一个？", "Find the red crate on the right side.")
    results["object_with_location"] = [dict(question=q, answer=find_candidate(index_path, scene, q, endpoint, model))
                                       for q in mixed]
    assert all(r["answer"] == {"entity": "103"} for r in results["object_with_location"])
    return dict(model=model, source_sha256=hashes, results=results, region_found=f"{accepted}/2",
                absent_returned_none=f"{rejected}/2", invalid_replies_rejected=5,
                entity_from_unsent_view_rejected=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--index", type=Path, default=ROOT / "output/npc-vision/index.json")
    parser.add_argument("--rebake-index", type=Path, help="可选，对重新训练导出的图片做相同对照")
    parser.add_argument("--endpoint", required=True)
    parser.add_argument("--model", required=True)
    parser.add_argument("--model-files", nargs="+", type=Path, help="运行该服务时加载的 GGUF 权重和视觉投影文件")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    if urlsplit(args.endpoint).hostname not in ("127.0.0.1", "::1", "localhost"):
        parser.error("此测试使用本机视觉模型")
    result = measure(args.index, args.endpoint, args.model)
    if args.model_files:
        result["weights_sha256"] = {}
        for file in args.model_files:
            with file.open("rb") as weights:
                result["weights_sha256"][file.name] = hashlib.file_digest(weights, "sha256").hexdigest()
    runs = [result]
    if args.rebake_index:
        result["independent_bake"] = measure(args.rebake_index, args.endpoint, args.model)
        runs.append(result["independent_bake"])
    report = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
    if args.report:
        args.report.write_text(report, encoding="utf-8")
    print(report)
    sys.exit(any(run["region_found"] != "2/2" or run["absent_returned_none"] != "2/2" for run in runs))
