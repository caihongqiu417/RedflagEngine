"""Redflag 本机模型服务。运行 python scripts/decision_service.py --help 查看连接方式。"""
import argparse
import base64
import binascii
from concurrent.futures import ThreadPoolExecutor
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
import threading
import time
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen
from uuid import uuid4


class InvalidChoice(ValueError):
    pass


class ProviderFailure(RuntimeError):
    pass


# ---- 游戏发来的观察与候选 ----
def validate(request, choices=True):
    if not isinstance(request, dict) or not isinstance(request.get("prompt"), str) or not request["prompt"].strip():
        raise ValueError("prompt_required")
    if not isinstance(request.get("instructions", ""), str):
        raise ValueError("invalid_instructions")
    for field in ("candidates", "observations"):
        entries = request.get(field, [])
        if not isinstance(entries, list):
            raise ValueError("invalid_" + field)
        ids = set()
        for item in entries:
            if (not isinstance(item, dict) or not isinstance(item.get("id"), str) or not item["id"].strip()
                    or not isinstance(item.get("description"), str) or item["id"] in ids):
                raise ValueError("invalid_" + field)
            ids.add(item["id"])
    if choices and not request.get("candidates"):
        raise ValueError("candidates_required")
    images = request.get("images", [])
    if not isinstance(images, list):
        raise ValueError("invalid_images")
    seen = set()
    for image in images:
        if (not isinstance(image, dict) or not isinstance(image.get("id"), str) or not image["id"]
                or image["id"] in seen or image.get("mime") not in ("image/png", "image/jpeg")):
            raise ValueError("invalid_image")
        seen.add(image["id"])
        try:
            data = base64.b64decode(image["data"], validate=True)
        except (KeyError, ValueError, TypeError, binascii.Error):
            raise ValueError("invalid_image") from None
        signature = b"\x89PNG\r\n\x1a\n" if image["mime"] == "image/png" else b"\xff\xd8\xff"
        if not data.startswith(signature):
            raise ValueError("invalid_image")


def messages(request, choices=True):
    author = request.get("instructions", "")
    text = request["prompt"]
    if choices:
        author, text = choice_prompt(request, author)
    images = request.get("images", [])
    content = [{"type":"text", "text":text}] if images else text
    for image in images:
        content.extend([{"type":"text", "text":"Image ID: " + image["id"]},
                        {"type":"image_url", "image_url":{"url":"data:" + image["mime"] + ";base64," + image["data"]}}])
    return [{"role":"system", "content":author}, {"role":"user", "content":content}]


def choice_prompt(request, author):
    ids = [c["id"] for c in request["candidates"]]
    rules = "You select a game action. Return only one of these exact IDs: " + json.dumps(ids, ensure_ascii=False)
    # 角色设定由游戏作者提供。当前观察、玩家的原话和图片均放在本轮输入中。
    observations = [(item["id"], item["description"]) for item in request.get("observations", [])]
    candidates = [(item["id"], item["description"]) for item in request["candidates"]]
    text = ("Observations: " + json.dumps(observations, ensure_ascii=False) + "\n"
            + request["prompt"] + "\nCandidates (ID, description): " + json.dumps(candidates, ensure_ascii=False)
            + "\nAnswer with the ID only.")
    return author + "\n" + rules, text


# ---- 模型推理 ----
class LocalModel:
    images = False

    def __init__(self, path, threads):
        from pathlib import Path
        import torch
        from transformers import AutoModelForCausalLM, AutoTokenizer

        self.name = Path(path).name
        torch.set_num_threads(threads)
        self.tokenizer = AutoTokenizer.from_pretrained(path, local_files_only=True, trust_remote_code=False)
        self.model = AutoModelForCausalLM.from_pretrained(path, local_files_only=True, trust_remote_code=False,
                                                         dtype=torch.float32).eval()

    def generate(self, conversation, cancel, limit, allowed=None):
        import torch
        from transformers import StoppingCriteria, StoppingCriteriaList

        class Cancelled(StoppingCriteria):
            def __call__(self, input_ids, scores, **kwargs):
                return cancel.is_set()

        prompt = self.tokenizer.apply_chat_template(conversation, tokenize=False, add_generation_prompt=True,
                                                    enable_thinking=False)
        inputs = self.tokenizer(prompt, return_tensors="pt")
        # 推理时只允许拼出本次提供的编号，多个编号有相同前缀时，下一步仍可继续区分。
        prefix_size = inputs["input_ids"].shape[1]
        end = self.tokenizer.eos_token_id
        def next_tokens(_, sequence):
            prefix = sequence[prefix_size:].tolist()
            return [tokens[len(prefix)] if len(tokens) > len(prefix) else end
                    for tokens in allowed if tokens[:len(prefix)] == prefix] or [end]

        with torch.inference_mode():
            output = self.model.generate(**inputs, do_sample=False, max_new_tokens=limit,
                                         pad_token_id=end, prefix_allowed_tokens_fn=next_tokens if allowed else None,
                                         stopping_criteria=StoppingCriteriaList([Cancelled()]))
        return self.tokenizer.decode(output[0, prefix_size:], skip_special_tokens=True)

    def choose(self, request, cancel):
        # 编号只在这一轮使用，返回前还原成作者给的候选 ID。
        options = request["candidates"]
        local = dict(request, candidates=[{"id":str(i+1),"description":v["description"]} for i,v in enumerate(options)])
        allowed = [self.tokenizer.encode(v["id"], add_special_tokens=False) for v in local["candidates"]]
        answer = self.generate(messages(local), cancel, max(map(len, allowed))+1, allowed)
        return options[int(answer)-1]["id"]

    def reply(self, request, cancel):
        return self.generate(messages(request, choices=False), cancel, 512)


class ChatModel:
    def __init__(self, endpoint, name, images):
        self.endpoint, self.name, self.images = endpoint, name, images

    def choose(self, request, cancel):
        choices = [item["id"] for item in request["candidates"]]
        schema = {"type":"object","properties":{"choice":{"type":"string","enum":choices}},
                  "required":["choice"],"additionalProperties":False}
        format = {"type":"json_schema","json_schema":{"name":"decision","strict":True,"schema":schema}}
        return json.loads(self.complete(messages(request), cancel, 128, format))["choice"]

    def reply(self, request, cancel):
        return self.complete(messages(request, choices=False), cancel, 512)

    def complete(self, conversation, cancel, limit, output_format=None):
        body = {"model":self.name, "messages":conversation, "temperature":0, "max_tokens":limit, "stream":True}
        if output_format: body["response_format"] = output_format
        headers = {"Content-Type":"application/json"}
        if token := os.environ.get("REDFLAG_MODEL_TOKEN"):
            headers["Authorization"] = "Bearer " + token
        call = Request(self.endpoint, json.dumps(body).encode(), headers)
        pieces = []
        try:
            stream = urlopen(call, timeout=30)
        except HTTPError as error:
            raise ProviderFailure(provider_error(error)) from None
        with stream as reply:
            for line in reply:
                if cancel.is_set():
                    break
                if line.startswith(b"data:"):
                    data = line[5:].strip()
                    if data == b"[DONE]":
                        break
                    choices = json.loads(data).get("choices", [])
                    if choices:
                        pieces.append(choices[0]["delta"].get("content") or "")
        return "".join(pieces)


# ---- 请求队列与取消 ----
class Decisions:
    def __init__(self, model):
        self.model = model
        self.jobs = {}
        self.lock = threading.Lock()
        self.pool = ThreadPoolExecutor(max_workers=1)

    def start(self, route, request):
        validate(request, choices=route == "decisions")
        if request.get("model") not in (None, "", self.model.name):
            raise ValueError("model_unavailable")
        if request.get("images") and not self.model.images:
            raise ValueError("images_unsupported")
        cancel = threading.Event()
        key = uuid4().hex
        with self.lock:
            self.jobs[(route,key)] = (cancel, self.pool.submit(self.answer, route, request, cancel), time.monotonic())
            # 已离开的客户端可能没能归还请求，只清理五分钟前已经结束的回答。
            for stale in [k for k, (_, future, started) in self.jobs.items()
                          if future.done() and time.monotonic() - started > 300]:
                del self.jobs[stale]
        return key

    def answer(self, route, request, cancel):
        if route == "replies":
            text = self.model.reply(request, cancel)
            if not text.strip(): raise ValueError("empty_reply")
            return {"text":text}
        choice = self.model.choose(request, cancel).strip()
        if choice not in {item["id"] for item in request["candidates"]}:
            raise InvalidChoice("invalid_choice")
        return {"choice":choice}

    def result(self, key):
        with self.lock:
            _, future, _ = self.jobs[key]
            if not future.done():
                return {"status":"pending"}
            try:
                return {"status":"completed", **future.result()}
            except InvalidChoice:
                return {"status":"failed", "error":"invalid_choice"}
            except ProviderFailure as error:
                return {"status":"failed", "error":str(error)}
            except (URLError, TimeoutError):
                return {"status":"failed", "error":"provider_unavailable"}
            except Exception:
                return {"status":"failed", "error":"model_failed"}

    def discard(self, key):
        with self.lock:
            # 排队中的任务可以立即撤回。已开始生成时由模型检查 cancel，编号先从查询列表移除。
            cancel, future, _ = self.jobs.pop(key)
            cancel.set()
            future.cancel()

    def close(self):
        with self.lock:
            for cancel, _, _ in self.jobs.values():
                cancel.set()
        self.pool.shutdown(wait=True, cancel_futures=True)


def provider_error(error):
    if error.code in (401,403): return "provider_unauthorized"
    if error.code == 429: return "provider_busy"
    if error.code >= 500: return "provider_unavailable"
    # llama.cpp 会报告这轮提示所需的 token 数。游戏只需知道应缩短输入或换更大的上下文。
    try:
        detail = json.loads(error.read(16384)).get("error")
        if isinstance(detail,dict) and detail.get("type") == "exceed_context_size_error":
            return "context_too_large"
    except (ValueError,UnicodeError): pass
    return "provider_rejected_request"


# ---- 供游戏访问的本机接口 ----
class Handler(BaseHTTPRequestHandler):
    def reply(self, code, value):
        data = json.dumps(value, ensure_ascii=False).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_POST(self):
        if self.path not in ("/v1/decisions", "/v1/replies"):
            return self.reply(404, {"error":"not_found"})
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if not 0 < length <= 12*1024*1024:
                return self.reply(413, {"error":"invalid_body_size"})
            request = json.loads(self.rfile.read(length))
            self.reply(202, {"id":self.server.decisions.start(self.path.split("/")[-1], request)})
        except (UnicodeError, json.JSONDecodeError):
            self.reply(400, {"error":"invalid_json"})
        except ValueError as e:
            self.reply(400, {"error":str(e)})

    def do_GET(self):
        if self.path == "/v1/health":
            model = self.server.decisions.model
            return self.reply(200, {"protocol":1, "model":model.name, "models":[model.name], "text":True, "images":model.images,
                                   "capabilities":["decisions","replies"]})
        self.job(remove=False)

    def do_DELETE(self):
        self.job(remove=True)

    def job(self, remove):
        path = self.path.split("/")
        if len(path) == 4 and path[1] == "v1" and path[2] in ("decisions","replies"):
            try:
                key = (path[2],path[3])
                if remove:
                    self.server.decisions.discard(key)
                    return self.reply(200, {"status":"removed"})
                return self.reply(200, self.server.decisions.result(key))
            except KeyError:
                pass
        self.reply(404, {"error":"not_found"})

    def log_message(self, *args):
        pass


class Server(ThreadingHTTPServer):
    def __init__(self, model, port):
        self.decisions = Decisions(model)
        super().__init__(("127.0.0.1", port), Handler)

    def server_close(self):
        super().server_close()
        self.decisions.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    provider = parser.add_mutually_exclusive_group(required=True)
    provider.add_argument("--local-model", help="本机 Transformers 聊天模型目录，首次启动时加载")
    provider.add_argument("--endpoint", help="兼容聊天服务的完整 chat/completions 地址")
    parser.add_argument("--model", help="聊天服务上的模型名称")
    parser.add_argument("--images", action="store_true", help="该聊天模型支持图片输入")
    parser.add_argument("--threads", type=int, default=6, help="本机 CPU 推理线程数")
    parser.add_argument("--port", type=int, default=8716)
    args = parser.parse_args()
    if args.endpoint and not args.model:
        parser.error("--endpoint 需要同时提供 --model")
    model = LocalModel(args.local_model, args.threads) if args.local_model else ChatModel(args.endpoint, args.model, args.images)
    with Server(model, args.port) as server:
        print(f"Redflag decision service: 127.0.0.1:{server.server_port} ({model.name})", flush=True)
        server.serve_forever()


if __name__ == "__main__":
    main()
