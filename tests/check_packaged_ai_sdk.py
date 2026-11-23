"""从空目录解压 SDK 并运行随包的正式程序集，核对文件清单与同一模型的文字、图像回答。"""
import argparse
import binascii
import hashlib
import json
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import time
from urllib.error import URLError
from urllib.request import Request, urlopen
from zipfile import ZipFile
import zlib

ROOT = Path(__file__).resolve().parents[1]


def red_image():
    def chunk(kind, content):
        return struct.pack(">I",len(content))+kind+content+struct.pack(">I",binascii.crc32(kind+content))
    head = struct.pack(">IIBBBBB",64,64,8,2,0,0,0)
    pixels = (b"\0"+bytes([255,0,0])*64)*64
    return b"\x89PNG\r\n\x1a\n"+chunk(b"IHDR",head)+chunk(b"IDAT",zlib.compress(pixels))+chunk(b"IEND",b"")


def unused_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1",0))
        return s.getsockname()[1]


def post(url, body, mime="application/json"):
    raw = json.dumps(body).encode() if mime=="application/json" else body
    with urlopen(Request(url,data=raw,headers={"Content-Type":mime}),timeout=15) as reply:
        return reply.read()


def ready(url):
    for _ in range(100):
        try:
            with urlopen(url,timeout=2) as reply: return json.load(reply)
        except URLError: time.sleep(.1)
    raise RuntimeError("packaged service unavailable")


def speech(args, sdk, work):
    port = unused_port()
    url = f"http://127.0.0.1:{port}/v1/speech"
    service = subprocess.Popen([args.speech_python,"-B",sdk/"scripts/speech_service.py","--port",str(port),
        "--runtime",args.speech_runtime.resolve()],cwd=work,creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        voices = ready(url+"/voices")
        assert voices["bank_speech"] == 4
        data = post(url+"/synthesize",{"text":"帮我留一份医疗包，等我过来再给我。","voice":args.voice})
        assert data.startswith(b"RIFF")
        text = json.loads(post(url+"/transcribe",data,"audio/wav"))["text"]
        assert "医疗包" in text,text
        job = json.loads(post(url+"/streams", {"text":"你好。","voice":args.voice}))
        size = 0
        while True:
            with urlopen(url+"/streams/"+job["id"], timeout=10) as reply:
                if reply.status == 204: break
                if reply.status == 200: size += len(reply.read())
                else: assert reply.status == 202
        assert size > 0 and size % 2 == 0
        return dict(voices=voices,voice_requested=args.voice,wav_bytes=len(data),stream_samples=size//2,text=text)
    finally:
        service.terminate()
        service.wait(timeout=8)


def run(args):
    with tempfile.TemporaryDirectory(prefix="redflag-empty-project-") as tmp, ZipFile(args.sdk) as zipfile:
        work = Path(tmp)
        for name in zipfile.namelist():
            if not (work/name).resolve().is_relative_to(work): raise ValueError("invalid archive path")
        zipfile.extractall(work)
        sdk = work/"Redflag-AI-Runtime"
        wanted = json.loads((sdk/"manifest.json").read_bytes())["files"]
        actual = {f.relative_to(sdk).as_posix():hashlib.sha256(f.read_bytes()).hexdigest()
                  for f in sdk.rglob("*") if f.is_file() and f.name!="manifest.json"}
        assert actual == wanted
        if args.speech_only:
            return dict(archive_sha256=hashlib.sha256(args.sdk.read_bytes()).hexdigest(),
                        clean_extraction=True, manifest_matches=True, speech=speech(args,sdk,work))
        port = unused_port()
        gateway = subprocess.Popen([sys.executable,"-B",sdk/"scripts/decision_service.py","--port",str(port),
                    "--model",args.model,"--endpoint",args.endpoint,"--images"],cwd=work,creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            base = f"http://127.0.0.1:{port}/v1"
            health = ready(base+"/health")
            assert sorted(health["capabilities"]) == ["decisions","replies"]
            choice = json.loads(post(base+"/decisions",{"prompt":"请选能打开柜子的物品。",
                "candidates":[{"id":"cup","description":"玻璃杯"},{"id":"key","description":"柜门钥匙"}]}))
            job = base+"/decisions/"+choice["id"]
            for _ in range(400):
                with urlopen(job,timeout=4) as reply: decision=json.load(reply)
                if decision["status"]!="pending": break
                time.sleep(.1)
            assert decision == {"status":"completed","choice":"key"},decision
            with urlopen(Request(job,method="DELETE"),timeout=4) as reply: assert reply.code==200
            image = work/"red.png"
            image.write_bytes(red_image())
            replies = []
            for question,extra,expected in (("请用一句中文介绍储物柜的钥匙，必须保留钥匙一词。",[],"钥匙"),
                                            ("这张图是什么颜色？请用中文只说一种颜色。",[image],"红")):
                proc = subprocess.run([sdk/"ModelConsole.exe",question,str(port),*extra],cwd=work,capture_output=True,timeout=80)
                if proc.returncode: raise RuntimeError(proc.stderr.decode("utf8",errors="replace"))
                text = proc.stdout.decode("utf-8-sig").strip()
                assert expected in text,text
                replies.append(dict(question=question,text=text))
        finally:
            gateway.terminate()
            gateway.wait(timeout=8)
        audio = speech(args,sdk,work) if args.speech_runtime else None
    return dict(archive_sha256=hashlib.sha256(args.sdk.read_bytes()).hexdigest(),file_count=len(wanted)+1,
                release_dll_sha256=wanted["Redflag-ScriptCore.dll"],runtime="Windows .NET Framework",model=args.model,
                clean_extraction=True,manifest_matches=True,health=health,replies=replies,decision=decision,speech=audio)


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--sdk",type=Path,default=ROOT/"output/releases/Redflag-AI-Runtime-SDK.zip")
    p.add_argument("--endpoint",default="http://127.0.0.1:18157/v1/chat/completions")
    p.add_argument("--model",default="Qwen3-VL-2B-Instruct")
    p.add_argument("--speech-runtime",type=Path)
    p.add_argument("--speech-only",action="store_true",help="仅检查独立语音宿主，可在不启动语言模型时运行")
    p.add_argument("--speech-python",default=sys.executable)
    p.add_argument("--voice",default="")
    p.add_argument("--out",type=Path,default=ROOT/"output/validation/runtime-ai/portable-sdk.json")
    args = p.parse_args()
    if args.speech_only and not args.speech_runtime: p.error("--speech-only needs --speech-runtime")
    result = run(args)
    args.out.parent.mkdir(parents=True,exist_ok=True)
    args.out.write_text(json.dumps(result,ensure_ascii=False,indent=2)+"\n",encoding="utf8")
    print(args.out)
