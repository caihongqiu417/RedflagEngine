"""从仓库根目录运行。用实际模型验证候选选择和图片，再用错误回复验证游戏怎样处理失败。"""
import argparse
import base64
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import threading
import time
from urllib.error import HTTPError
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from decision_service import ChatModel, LocalModel, Server


def call(url, method="GET", body=None):
    req = Request(url, json.dumps(body).encode() if body is not None else None,
                  {"Content-Type":"application/json"}, method=method)
    with urlopen(req,timeout=5) as response:
        return json.load(response)


def decision(url, body):
    job = url + "/v1/decisions/" + call(url+"/v1/decisions","POST",body)["id"]
    start = time.monotonic()
    try:
        while time.monotonic()-start < 60:
            result = call(job)
            if result["status"] != "pending":
                result["elapsed_ms"] = round((time.monotonic()-start)*1000)
                return result
            time.sleep(.025)
        raise TimeoutError("model response")
    finally:
        call(job,"DELETE")


def run(model, checks):
    with Server(model, 0) as server:
        worker = threading.Thread(target=server.serve_forever)
        worker.start()
        try:
            return checks(f"http://127.0.0.1:{server.server_port}",server.server_port)
        finally:
            server.shutdown()
            worker.join()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--local-model", type=Path)
    source.add_argument("--endpoint")
    parser.add_argument("--model")
    parser.add_argument("--images", action="store_true")
    parser.add_argument("--cpp", type=Path, default=ROOT / "bin-int/Tests/Decision/DecisionClientTests.exe")
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    model = LocalModel(args.local_model,6) if args.local_model else ChatModel(args.endpoint,args.model,args.images)
    items = [{"id":"mug","description":"a ceramic drinking cup"},{"id":"key","description":"a brass cabinet key"}]
    query = {"instructions":"You are a game shopkeeper.", "candidates":items, "observations":[
        {"id":"cabinet","description":"A locked cabinet on the shelf."}]}
    index = ROOT / "output/npc-vision/index.json"
    frame = index.parent / json.loads(index.read_bytes())["views"][0]["image"]
    png = frame.read_bytes()
    def live(url, port):
        report = dict(health=call(url+"/v1/health"), cases=[])
        assert report["health"]["models"] == [model.name]
        try:
            call(url+"/v1/decisions","POST",dict(query,prompt="choose",model="missing-model"))
            raise AssertionError("unconfigured model was accepted")
        except HTTPError as error:
            assert error.code == 400 and json.load(error)["error"] == "model_unavailable"
            report["unknown_model_error"] = "model_unavailable"
        for prompt,wanted,offered in (("Pick the item that unlocks the cabinet.","key",items),
                                      ("Pick the item that unlocks the cabinet.","key",list(reversed(items))),
                                      ("The player needs to drink water. Offer the drinking vessel.","mug",items)):
            result = decision(url,dict(query,prompt=prompt,candidates=offered,model=model.name))
            report["cases"].append(dict(prompt=prompt,candidates=[c["id"] for c in offered],expected=wanted,**result))
        check = subprocess.run([str(args.cpp.resolve()),str(port),"choose"],timeout=100)
        report["cpp_choice_cancel_stale_scene"] = check.returncode == 0
        photo = dict(prompt="What color is the small crate in this image? Reply with its color ID.",
                     candidates=[{"id":"red","description":"red crate"},{"id":"blue","description":"blue crate"}],
                     images=[{"id":"workshop","mime":"image/png","data":base64.b64encode(png).decode()}])
        if model.images:
            # 保持提示、候选和构图不变，只交换箱体所在图中的红蓝通道，检验模型有没有读图片。
            from PIL import Image
            from io import BytesIO
            with Image.open(frame) as image:
                red,green,blue = image.convert("RGB").split()
                out = BytesIO()
                Image.merge("RGB",(blue,green,red)).save(out, format="PNG")
            control = out.getvalue()
            results = []
            for pixels,wanted in ((png,"red"),(control,"blue")):
                photo["images"][0]["data"] = base64.b64encode(pixels).decode()
                result = decision(url,photo)
                results.append(dict(expected=wanted,sha256=hashlib.sha256(pixels).hexdigest(),**result))
                assert result.get("choice") == wanted, result
            report["vision"] = dict(image=frame.relative_to(ROOT).as_posix(),controls=results)
        else:
            try:
                call(url+"/v1/decisions","POST",photo)
                raise AssertionError("text-only provider accepted an image")
            except HTTPError as error:
                assert error.code == 400 and json.load(error)["error"] == "images_unsupported"
                report["image_error"] = "images_unsupported"
        return report
    report = run(model,live)

    class InvalidProvider:
        name, images = "invalid-output-test", False
        def choose(self, request, cancel): return "not-an-offered-id"

    def invalid(url, port):
        subprocess.run([str(args.cpp.resolve()),str(port),"failure"],check=True,timeout=20)
        return decision(url,dict(query,prompt="choose one"))
    report["invalid_output_fixture"] = run(InvalidProvider(),invalid)
    assert report["invalid_output_fixture"]["error"] == "invalid_choice"
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n",encoding="utf8")
    print(args.out)
    assert all(r.get("choice") == r["expected"] for r in report["cases"]) and report["cpp_choice_cancel_stale_scene"], report["cases"]


if __name__ == "__main__":
    main()
