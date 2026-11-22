"""从空目录启动归档中的三个服务，实际检查游戏的文字、图片与中文语音请求。"""
import argparse
from array import array
import base64
import hashlib
from io import BytesIO
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from urllib.request import Request, urlopen
import wave
import zipfile

from PIL import Image
import psutil

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"scripts"))
from package_ai_sdk import SCRIPTS


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream,"sha256").hexdigest()


def request(url,body=None,method=None):
    raw = isinstance(body,bytes)
    headers = {"Content-Type":"audio/wav" if raw else "application/json"}
    payload = body if raw or body is None else json.dumps(body,ensure_ascii=False).encode()
    with urlopen(Request(url,payload,headers,method=method),timeout=45) as response:
        if response.headers.get_content_type()=="audio/wav": return response.read()
        return json.load(response)


def ask(url,kind,body):
    route = f"{url}/v1/{kind}"
    ticket = request(route,body)["id"]
    start = time.monotonic()
    try:
        while time.monotonic()-start<90:
            answer = request(f"{route}/{ticket}")
            if answer["status"]!="pending":
                assert answer["status"]=="completed",answer
                return answer
            time.sleep(.05)
        raise TimeoutError(kind)
    finally:
        request(f"{route}/{ticket}",method="DELETE")


def available_ports():
    # 同时占住三个随机端口再一起归还；启动后再按监听进程的实际路径核实归属。
    sockets = [socket.socket() for _ in range(3)]
    try:
        for listener in sockets: listener.bind(("127.0.0.1",0))
        return [listener.getsockname()[1] for listener in sockets]
    finally:
        for listener in sockets: listener.close()


def children(project):
    owned = []
    for process in psutil.process_iter(["exe"]):
        exe = process.info["exe"]
        if exe and Path(exe).is_relative_to(project/"CompanionRuntime"):
            owned.append(process)
    return owned


def actual_services(project,ports):
    rows = {}
    owned = {p.pid:p for p in children(project)}
    for con in psutil.net_connections(kind="tcp"):
        if con.status!=psutil.CONN_LISTEN or con.laddr.port not in ports: continue
        process = owned[con.pid]
        args = [s.replace(str(project),"PROJECT") for s in process.cmdline()]
        rows[con.laddr.port] = dict(exe=Path(process.exe()).relative_to(project).as_posix(),args=args)
    assert set(rows)==set(ports),rows
    decision,speech,llama = (rows[p] for p in ports)
    assert decision["exe"]==speech["exe"]=="CompanionRuntime/python/python.exe"
    assert llama["exe"]=="CompanionRuntime/llama/llama-server.exe"
    assert any(s.endswith("speech_service.py") for s in speech["args"]) and any(Path(s)==Path("PROJECT/CompanionRuntime/speech") for s in speech["args"])
    weights = [Path(s).name for s in llama["args"] if Path(s).parent==Path("PROJECT/CompanionRuntime/models")]
    assert "mmproj-Qwen3VL-2B-Instruct-Q8_0.gguf" in weights and "Qwen3VL-2B-Instruct-Q4_K_M.gguf" in weights
    return {"decision":decision,"speech":speech,"llama":llama}


def validate(project,ports):
    decisions,speech = (f"http://127.0.0.1:{p}" for p in ports[:2])
    health = request(decisions+"/v1/health")
    assert health["images"] and "replies" in health["capabilities"]
    prompt = "柜门锁着。请把能开锁的那件东西交给我。"
    items = [{"id":"mug","description":"杯子，用来喝水"},{"id":"key","description":"钥匙，用来打开柜门"}]
    choice = ask(decisions,"decisions",dict(prompt=prompt,candidates=items))
    assert choice["choice"]=="key",choice

    # 同一张真实场景图只交换红、蓝两个通道。用同一句提问看模型的回答是否跟着变。
    index = ROOT/"output/npc-vision/index.json"
    frame = index.parent/json.loads(index.read_bytes())["views"][0]["image"]
    original = frame.read_bytes()
    with Image.open(BytesIO(original)) as picture:
        red,green,blue = picture.convert("RGB").split()
        alternate = BytesIO()
        Image.merge("RGB",(blue,green,red)).save(alternate,"PNG")
    image_prompt = "图中小箱子是什么颜色？用一个中文颜色词回答。"
    images = []
    for pixels,color in ((original,"红"),(alternate.getvalue(),"蓝")):
        reply = ask(decisions,"replies",dict(prompt=image_prompt,images=[dict(id="workshop",mime="image/png",data=base64.b64encode(pixels).decode())]))
        assert color in reply["text"],reply
        images.append(dict(sha256=hashlib.sha256(pixels).hexdigest(),expected=color,reply=reply))

    voices = request(speech+"/v1/speech/voices")
    voice = next((v for v in voices["voices"] if any(n in v.lower() for n in ("huihui","kangkang","yaoyao","chinese"))),None)
    if not voice: raise RuntimeError("本机缺少可合成中文的 Windows 系统音色。")
    sentence = "请打开仓库的门。"
    pcm = request(speech+"/v1/speech/synthesize",dict(text=sentence,voice=voice))
    with wave.open(BytesIO(pcm)) as wav:
        assert (wav.getframerate(),wav.getnchannels(),wav.getsampwidth())==(16000,1,2)
        samples = array("h",wav.readframes(wav.getnframes()))
    heard = request(speech+"/v1/speech/transcribe",pcm)
    assert "打开" in heard["text"] and "门" in heard["text"] and max(map(abs,samples))>100,heard
    return dict(health=health,decision=dict(prompt=prompt,candidates=items,reply=choice),
                vision=dict(source=frame.relative_to(ROOT).as_posix(),prompt=image_prompt,images=images),
                speech=dict(voices=voices,voice=voice,sentence=sentence,samples=len(samples),sample_rate=16000,
                            peak_pcm=max(map(abs,samples)),wav_sha256=hashlib.sha256(pcm).hexdigest(),transcript=heard)),pcm


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive",type=Path)
    parser.add_argument("--output",type=Path,default=ROOT/"output/validation/packaged-companion")
    args = parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="redflag-companion-") as folder:
        project = Path(folder)
        with zipfile.ZipFile(args.archive) as bundle: bundle.extractall(project)
        scripts = project/"scripts"
        scripts.mkdir()
        for name in SCRIPTS: shutil.copyfile(ROOT/"scripts"/name,scripts/name)
        ports = available_ports()
        env = {k:v for k,v in os.environ.items() if not k.startswith(("REDFLAG_","PYTHON"))}
        start = time.monotonic()
        try:
            command = ["powershell.exe","-NoProfile","-NonInteractive","-ExecutionPolicy","Bypass","-File",
                       str(scripts/"Start-NpcServices.ps1"),"-RequireAll","-DecisionPort",str(ports[0]),
                       "-SpeechPort",str(ports[1]),"-LlamaPort",str(ports[2])]
            # Windows 后台服务会继承启动器的标准句柄。用普通日志文件，PowerShell 退出即可开始发请求。
            startup = project/"startup.txt"
            with startup.open("wb") as console:
                run = subprocess.run(command,cwd=project,env=env,stdout=console,stderr=subprocess.STDOUT,timeout=240)
            if run.returncode:
                errors = [f.name+"\n"+f.read_text(encoding="utf8",errors="replace")[-1500:] for f in (project/"tmp/npc-services").glob("*.err")]
                raise RuntimeError(startup.read_text(errors="replace")+"\n"+"\n".join(errors))
            ready = round(time.monotonic()-start,2)
            services = actual_services(project,ports)
            result,audio = validate(project,ports)
        finally:
            owned = children(project)
            for process in owned:
                try: process.terminate()
                except psutil.NoSuchProcess: pass
            _,alive = psutil.wait_procs(owned,timeout=8)
            if alive: raise RuntimeError("本测试仍有子进程在运行。")
        runtime = project/"CompanionRuntime"
        parts = list((runtime/"models").glob("*.gguf")) + list((runtime/"speech/models").glob("*.onnx"))
        parts += [runtime/"speech/DeliverySpeechRuntime.dll",runtime/"llama/llama-server.exe",runtime/"python/python.exe"]
        report = dict(archive=args.archive.name,archive_sha256=digest(args.archive),startup_seconds=ready,ports=ports,
                      runtime_sha256={p.relative_to(project).as_posix():digest(p) for p in parts},
                      services=services,processes_stopped=len(owned),scripts={f:digest(scripts/f) for f in SCRIPTS},**result)
    (args.output/"synthesized.wav").write_bytes(audio)
    (args.output/"check.json").write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n",encoding="utf8")
    print(f"PASS {report['health']['model']} / {result['speech']['transcript']} / stopped {len(owned)}")


if __name__=="__main__":
    main()
