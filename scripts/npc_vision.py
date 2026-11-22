"""按场景和关键词选取 NPC 拍过的图，返回图中候选实体或区域。"""
import argparse
import base64
from io import BytesIO
import json
import math
import os
from pathlib import Path
import re
from urllib.request import Request, urlopen
from PIL import Image


def image_content(png):
    return {"type": "image_url", "image_url": {"url": "data:image/png;base64," + base64.b64encode(png).decode("ascii")}}


def ask_model(content, endpoint, model):
    body = {"model": model, "messages": [{"role": "user", "content": content}], "temperature": 0, "max_tokens": 24}
    headers = {"Content-Type": "application/json"}
    if token := os.environ.get("REDFLAG_VISION_TOKEN"):
        headers["Authorization"] = "Bearer " + token
    request = Request(endpoint, json.dumps(body).encode(), headers)
    with urlopen(request, timeout=60) as response:
        return json.load(response)["choices"][0]["message"]["content"].strip()


def find_candidate(index_path, scene, question, endpoint, model, *, include_regions=True):
    index = json.loads(index_path.read_bytes())
    empty = {"entity": None}
    if index["scene"] != scene:
        return empty
    words = question.casefold()
    def matches(tags):
        return sum(tag.casefold() in words for tag in tags)
    ranked = []
    for view in index["views"]:
        regions = {key: r for key, r in view.get("regions", {}).items() if include_regions and matches(r["tags"])}
        ranked.append((matches(view["tags"]) + sum(matches(r["tags"]) for r in regions.values()), view, regions))
    ranked.sort(key=lambda item: -item[0])
    # 最多两张图可以从两个方向观察同一个物体，也限制了模型每次需要读入的图像量。
    views = [(view, regions) for score, view, regions in ranked[:2] if score]
    if not views:
        return empty
    entities = {key: label for view, _ in views for key, label in view.get("entities", {}).items()}
    content, references, patches = [], {}, {}
    next_region = max((int(key) for key in entities), default=0) + 1
    for view, regions in views:
        image = (index_path.parent / view["image"]).read_bytes()
        content.append(image_content(image))
        if regions:
            choices = []
            for key, region in regions.items():
                # 区域编号只在这次提问中使用，避开当前图片里已经分配给实体的编号。
                token = str(next_region)
                next_region += 1
                references[token] = dict(camera=view["camera"], id=key, label=region["label"],
                                         origin=[row[3] for row in view["pose"][:3]], direction=region["direction"])
                patches[token] = (image, view["size"], region["bounds"])
                choices.append(f"{token}: {region['label']}, bounds {region['bounds']}")
            content.append({"type": "text", "text": f"Regions in the preceding image, size {view['size']}, "
                            "bounds in pixels from top left (left, top, right, bottom): " + "; ".join(choices)})
    if references:
        choices = {**entities, **{code: r["label"] for code, r in references.items()}}
        prompt = (f"Visible object labels: {json.dumps(choices, ensure_ascii=False)}. {question} "
                  "Reply with only the decimal ID of the matching visible object or region, or none.")
    else:
        prompt = (f"{question}\nVisible object labels: {json.dumps(entities, ensure_ascii=False)}. "
                  "Reply with only the decimal ID of the matching visible object, or none.")
    content.append({"type": "text", "text": prompt})
    answer = ask_model(content, endpoint, model)
    # 世界方向来自拍摄机位与标注框。模型只能从本次发送的图里选编号。
    for code, region in references.items():
        if answer in (f"{code}: {region['label']}", f"{code}：{region['label']}"):
            answer = code
    match = re.fullmatch(r"([0-9]+)[.。]?", answer)
    if match and match[1] in references:
        region = references[match[1]]
        png, size, bounds = patches[match[1]]
        # 再给模型看一次选中框里的像素，检查标注名称是否与照片中能看到的内容相符。
        with Image.open(BytesIO(png)) as img:
            sx, sy = img.width / size[0], img.height / size[1]
            left, top, right, bottom = bounds
            crop = img.crop((math.floor(left * sx), math.floor(top * sy),
                             math.ceil(right * sx), math.ceil(bottom * sy)))
            buffer = BytesIO()
            crop.save(buffer, format="PNG")
        check = (f"What does this picture show? Choose exactly one: A, empty, blank, completely black, or unrelated content. "
                 f"B, the specified surface or object: {region['label']}. Reply A or B.")
        visible = ask_model([image_content(buffer.getvalue()), {"type": "text", "text": check}], endpoint, model)
        return {"entity": None, "region": region} if visible.rstrip(".。") == "B" else empty
    return {"entity": match[1]} if match and match[1] in entities else empty


def candidate(index_path, scene, question, endpoint, model):
    return find_candidate(index_path, scene, question, endpoint, model, include_regions=False)["entity"]


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="从已经拍好的视角图中复查一次识别")
    parser.add_argument("index", type=Path)
    parser.add_argument("scene")
    parser.add_argument("question")
    parser.add_argument("--endpoint", default="http://127.0.0.1:8080/v1/chat/completions")
    parser.add_argument("--model", required=True)
    args = parser.parse_args()
    print(json.dumps(find_candidate(args.index, args.scene, args.question, args.endpoint, args.model), ensure_ascii=False))
