"""本机端口上的协议测试。分别改变模型是否可用和输出格式，复查同一客户端能继续提问。"""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import sys
import threading
import time
from urllib.error import HTTPError
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"scripts"))
from decision_service import ChatModel, Server


class ModelStub(BaseHTTPRequestHandler):
    mode = "ok"

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        if self.mode in ("context","busy","provider_down"):
            self.send_response({"context":400,"busy":429,"provider_down":503}[self.mode])
            self.end_headers()
            self.wfile.write(b'{"error":{"type":"exceed_context_size_error"}}')
            return
        value = {"choices":[{"delta":{"content":'{"choice":"key"}' if "response_format" in body else "一把铜钥匙。"}}]}
        self.send_response(200)
        self.end_headers()
        self.wfile.write(("data: "+json.dumps(value)+"\n\ndata: [DONE]\n\n").encode())

    def log_message(self,*args): pass


def call(address, method="GET", data=None):
    try:
        with urlopen(Request(address,method=method,data=None if data is None else json.dumps(data).encode()),timeout=3) as r:
            return r.code,json.load(r)
    except HTTPError as r:
        return r.code,json.load(r)


def main():
    with ThreadingHTTPServer(("127.0.0.1",0),ModelStub) as upstream:
        threading.Thread(target=upstream.serve_forever,daemon=True).start()
        model = ChatModel(f"http://127.0.0.1:{upstream.server_port}/v1/chat/completions","test",False)
        with Server(model,0) as service:
            threading.Thread(target=service.serve_forever,daemon=True).start()
            url = f"http://127.0.0.1:{service.server_port}/v1"
            try:
                for mode, expected in (("context","context_too_large"),("busy","provider_busy"),("provider_down","provider_unavailable"),("ok",None)):
                    ModelStub.mode = mode
                    status, job = call(url+"/replies","POST",{"prompt":"描述这把钥匙"})
                    assert status == 202
                    path = url+"/replies/"+job["id"]
                    end = time.monotonic()+3
                    while time.monotonic()<end:
                        _,answer = call(path)
                        if answer["status"] != "pending": break
                        time.sleep(.01)
                    assert answer == ({"status":"failed","error":expected} if expected else {"status":"completed","text":"一把铜钥匙。"})
                    assert call(path) == (200,answer)
                    assert call(url+"/decisions/"+job["id"])[0] == 404
                    assert call(path,"DELETE")[0] == 200 and call(path)[0] == 404
                assert call(url+"/decisions","POST",{"prompt":"选道具"}) == (400,{"error":"candidates_required"})
                assert call(url+"/replies","POST",{"prompt":"提问","model":"elsewhere"}) == (400,{"error":"model_unavailable"})
            finally:
                service.shutdown()
                upstream.shutdown()
    print("PASS model errors, recovery, result ownership and cancellation")


if __name__ == "__main__":
    main()
