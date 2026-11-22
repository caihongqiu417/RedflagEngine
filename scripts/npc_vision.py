"""本机 NPC 视觉服务。按场景和关键词选图，把图片交给视觉模型，将候选实体编号还给游戏。"""
import argparse
import base64
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import re
from urllib.error import URLError
from urllib.request import Request, urlopen


def candidate(index_path, scene, question, endpoint, model):
    index = json.loads(index_path.read_bytes())
    if index["scene"] != scene:
        return None
    ranked = [(sum(tag.casefold() in question.casefold() for tag in v["tags"]), v) for v in index["views"]]
    ranked.sort(key=lambda item: -item[0])
    # 最多两张图可以从两个方向观察同一个物体，也限制了模型每次需要读入的图像量。
    views = [view for score, view in ranked[:2] if score]
    if not views:
        return None
    entities = {key: label for view in views for key, label in view["entities"].items()}
    content = [{"type": "image_url", "image_url": {
        "url": "data:image/png;base64," + base64.b64encode((index_path.parent / v["image"]).read_bytes()).decode("ascii")
    }} for v in views]
    content.append({"type": "text", "text": f"{question}\nVisible object labels: {json.dumps(entities, ensure_ascii=False)}. "
                    "Reply with only the decimal ID of the matching visible object, or none."})
    body = {"model": model, "messages": [{"role": "user", "content": content}], "temperature": 0, "max_tokens": 24}
    headers = {"Content-Type": "application/json"}
    if token := os.environ.get("REDFLAG_VISION_TOKEN"):
        headers["Authorization"] = "Bearer " + token
    request = Request(endpoint, json.dumps(body).encode(), headers)
    with urlopen(request, timeout=60) as response:
        answer = json.load(response)["choices"][0]["message"]["content"].strip()
    # 有些模型会把句号紧跟在编号后面。只接受单个十进制编号，并查回当前图片的候选表。
    match = re.fullmatch(r"([0-9]+)[.。]?", answer)
    return match[1] if match and match[1] in entities else None


def create_server(index, port, endpoint, model):
    class Handler(BaseHTTPRequestHandler):
        def do_POST(self):
            if self.path != "/candidate":
                self.send_error(404)
                return
            try:
                length = int(self.headers.get("Content-Length", "0"))
                if not 0 < length <= 16384:
                    raise ValueError("invalid request size")
                query = json.loads(self.rfile.read(length))
                result = candidate(index, query["scene"], query["question"], endpoint, model)
                data = json.dumps({"entity": result}).encode()
            except (KeyError, TypeError, ValueError) as error:
                self.send_error(400, str(error))
                return
            except (OSError, URLError) as error:
                self.send_error(502, str(error))
                return
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

    return ThreadingHTTPServer(("127.0.0.1", port), Handler)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("index", type=Path)
    parser.add_argument("--port", type=int, default=8715)
    parser.add_argument("--endpoint", default="http://127.0.0.1:8080/v1/chat/completions")
    parser.add_argument("--model", required=True)
    args = parser.parse_args()
    with create_server(args.index, args.port, args.endpoint, args.model) as server:
        server.serve_forever()
