"""在 Delivery 的 Windows 游戏包中向角色提交一段文字，保留游戏进程自己的计时事件。"""
import argparse
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.error import HTTPError
from urllib.request import Request, urlopen
from urllib.parse import urlsplit

PROMPT = "帮我留一份医疗包，等我过来再给我。"
ROOT = Path(__file__).resolve().parents[1]


def start(args, directory, stream, env=None):
    window = subprocess.STARTUPINFO()
    window.dwFlags = subprocess.STARTF_USESHOWWINDOW
    window.wShowWindow = 0
    return subprocess.Popen(args, cwd=directory, env=env, stdin=subprocess.DEVNULL, stdout=stream, stderr=stream,
                            creationflags=subprocess.CREATE_NO_WINDOW, startupinfo=window)


def stop(process):
    if process.poll() is None:
        process.terminate()
    process.wait(timeout=10)


def run(args):
    if urlsplit(args.model_endpoint).hostname != "127.0.0.1":
        raise ValueError("本机对照要求两个游戏使用同一台机器上的模型")
    folder = ROOT/"tmp"/"ue-npc-latency"
    folder.mkdir(parents=True,exist_ok=True)
    package = args.windows.resolve()
    exe = package/"Delivery/Binaries/Win64/Delivery.exe"
    paks = package/"Delivery/Content/Paks/Delivery-Windows.pak"
    assert exe.is_file() and paks.is_file(), "需要 Windows 游戏包目录"
    conversation = "/Game/Level/TestForCharacter.TestForCharacter:PersistentLevel."
    script = folder/"submit.txt"
    position = {"BP_ResidentShrewd_C":"(X=11760,Y=12650,Z=2018)",
                "BP_Companion_Ergou_C":"(X=-5370,Y=-20277,Z=3032)"}[args.npc]
    script.write_text("\n".join([
        f"ke BP_DeliveryMan_C K2_SetActorLocation {position} False () True",
        f"ke DeliveryConversationComponent Submit {conversation}{args.npc}_0 {PROMPT}"]),encoding="utf-8-sig")
    commands = "t.MaxFPS 60,"+"defer "*100+"exec "+os.path.relpath(script,exe.parent.parent).replace("\\","/")
    calls = []
    class TracedModel(BaseHTTPRequestHandler):
        def log_message(self, *args): pass
        def do_POST(self):
            body = self.rfile.read(int(self.headers["Content-Length"]))
            if args.guide_format:
                # 对照实验只限定第一条回答的字段。候选编号和台词仍由相同的模型决定。
                request = json.loads(body)
                schema = dict(type="object",properties=dict(candidate_index=dict(type="integer",minimum=0),
                    speech=dict(type="string"),emotion=dict(type="string",enum=["Calm","Angry","Surprised"])),
                    required=["candidate_index","speech","emotion"],additionalProperties=False)
                request["response_format"] = dict(type="json_schema",json_schema=dict(name="first_record",strict=True,schema=schema))
                body = json.dumps(request, ensure_ascii=False).encode("utf8")
            started = time.monotonic()
            try:
                upstream = urlopen(Request(args.model_endpoint,data=body,headers={"Content-Type":"application/json"}),timeout=30)
            except HTTPError as e:
                upstream = e
            with upstream:
                status = upstream.code
                self.send_response(status)
                self.send_header("Content-Type",upstream.headers.get("Content-Type","application/json"))
                self.end_headers()
                entry = dict(status=status, request_bytes=len(body))
                calls.append(entry)
                if status >= 400:
                    raw = upstream.read()
                    try:
                        error = json.loads(raw)["error"]
                        entry["reason"] = {k:error[k] for k in ("type","code","n_ctx","n_prompt_tokens","message") if k in error}
                    except (KeyError,ValueError,TypeError): pass
                    self.wfile.write(raw)
                else:
                    try:
                        for line in upstream:
                            self.wfile.write(line)
                            self.wfile.flush()
                    except (ConnectionError,OSError): pass
                entry["duration_ms"] = round((time.monotonic()-started)*1000,1)
    proxy = ThreadingHTTPServer(("127.0.0.1",0),TracedModel)
    threading.Thread(target=proxy.serve_forever,daemon=True).start()
    # 只传入 Windows/Python 运行所需的环境变量，让测试后端仅连接本机模型。
    env = {k:os.environ[k] for k in ("PATH","SystemRoot","TEMP","TMP","USERPROFILE","LOCALAPPDATA","APPDATA") if k in os.environ}
    env.update(DELIVERY_AI_MODEL_URL=f"http://127.0.0.1:{proxy.server_port}/v1/chat/completions", DELIVERY_AI_MODEL=args.model,
               DELIVERY_AI_DIALOGUE_LOG="1", DELIVERY_TTS_SHELL="powershell.exe", PYTHONUTF8="1",
               PYTHONIOENCODING="utf8", PYTHONDONTWRITEBYTECODE="1")
    logpath = folder/"game.log"
    logpath.unlink(missing_ok=True)
    engine = [str(exe),"/Game/Level/TestForCharacter","-unattended","-NoSteam","-nosplash","-windowed",
              "-ResX=640","-ResY=360","-RenderOffscreen",f"-abslog={logpath}",f"-userdir={folder/'user'}",
              "-ini:Game:[/Script/Delivery.DeliveryAiSettings]:bEnabled=True",
              f"-ini:Game:[/Script/Delivery.DeliveryAiSettings]:DecisionEndpoint=http://127.0.0.1:{args.port}/v1/decisions",
              "-ExecCmds="+commands]
    if args.null_rhi: engine += ["-nullrhi","-nosound"]
    # 让 Delivery 将本轮测试日志写进 Redflag 的临时目录，保留其正式校验、调度和合成代码。
    trace = folder / "dialogue.ndjson"
    trace.write_text("",encoding="utf8")
    launcher = """import json, pathlib, re, runpy, sys, traceback
tools = pathlib.Path(sys.argv.pop(1))
sys.path.insert(0, str(tools))
import dialogue_log
dialogue_log._path = pathlib.Path('dialogue.ndjson')
record = dialogue_log.record
def trace_error(*args, **kwargs):
    error = sys.exc_info()[1]
    if error is not None:
        frame = traceback.extract_tb(error.__traceback__)[-1]
        # 保存验证失败的位置；回答正文和模型请求留在本机。
        reason = str(error)
        if not re.fullmatch(r'[a-zA-Z0-9_ ]{1,96}', reason): reason = type(error).__name__
        fault = dict(request=args[0], source=args[1], reason=reason,
                     file=pathlib.Path(frame.filename).name, line=frame.lineno, function=frame.name)
        if reason == 'invalid performance':
            cause = error.__traceback__
            while cause.tb_next: cause = cause.tb_next
            speech = cause.tb_frame.f_locals.get('speech')
            emotion = cause.tb_frame.f_locals.get('emotion')
            fault.update(speech_chars=len(speech) if isinstance(speech, str) else None,
                         emotion_present=emotion is not None, emotion_allowed=emotion in ('Calm','Angry','Surprised'))
        print('validation '+json.dumps(fault), flush=True)
    return record(*args, **kwargs)
dialogue_log.record = trace_error
runpy.run_path(str(tools/'server.py'),run_name='__main__')"""
    with (folder/"provider.log").open("w",encoding="utf8") as stream:
        gateway = start([sys.executable,"-u","-c",launcher,str(args.delivery_tools.resolve()),"--port",str(args.port),"--provider",args.provider],folder,stream,env)
        try:
            time.sleep(1)
            if gateway.poll() is not None: raise RuntimeError("Delivery 模型服务启动失败，参见 tmp/ue-npc-latency/provider.log")
            host = start(engine,exe.parent,subprocess.DEVNULL)
            try:
                deadline = time.monotonic()+args.seconds
                while host.poll() is None and time.monotonic()<deadline:
                    time.sleep(.2)
                    if args.dispatch_only and logpath.is_file():
                        log=logpath.read_bytes()
                        begin=log.rfind(b"ResidentConversation accepted:")
                        if begin>=0 and re.search(rb"stage=http_start player=1\b",log[begin:]):
                            break
            finally:
                stop(host)
        finally:
            stop(gateway)
            proxy.shutdown()
            proxy.server_close()
    raw = logpath.read_bytes()
    markers = []
    pattern = re.compile(r"^\[([\d.\-:]+)\]\[\s*(\d+)\](.+)")
    for line in raw.decode("utf8",errors="replace").splitlines():
        match = pattern.match(line)
        if match and any(k in line for k in ("NpcLatency ","NpcSpeech text_only","ResidentConversation accepted","ResidentConversation rejected","Called 'Submit ","stream failed","TransportFailure")):
            stamp,frame,body = match.groups()
            t = datetime.strptime(stamp,"%Y.%m.%d-%H.%M.%S:%f")
            markers.append({"utc":t.isoformat(),"engine_frame":int(frame),"message":body})
    with exe.open("rb") as binary:
        digest = hashlib.file_digest(binary,"sha256").hexdigest()
    dialogues = []
    for line in trace.read_text(encoding="utf8").splitlines():
        captured = json.loads(line)
        if captured.get("source") == "player":
            entry = {k:captured.get(k) for k in ("request_id","model","error","chosen","chosen_candidate_id","speech")}
            entry["offered_actions"] = sorted({c.split(":")[0] for c in captured.get("candidates",[]) if isinstance(c,str)})
            dialogues.append(entry)
    return dict(engine="Delivery Unreal 5.8 packaged Windows", exe_sha256=digest,
                npc=args.npc, prompt=PROMPT, provider=args.provider, model=args.model, guided_first_record=args.guide_format,
                null_rhi=args.null_rhi, window=[640,360],
                markers=markers, model_calls=calls, player_responses=dialogues,
                validation_errors=[json.loads(row[11:]) for row in (folder/"provider.log").read_text(encoding="utf8").splitlines()
                                   if row.startswith("validation ")], raw_log_sha256=hashlib.sha256(raw).hexdigest())


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--windows",type=Path,required=True)
    parser.add_argument("--delivery-tools",type=Path,required=True)
    parser.add_argument("--model-endpoint",default="http://127.0.0.1:18157/v1/chat/completions")
    parser.add_argument("--model",default="Qwen3-VL-2B-Instruct")
    parser.add_argument("--provider",choices=["fixture","llm"],default="llm")
    parser.add_argument("--npc",choices=["BP_Companion_Ergou_C","BP_ResidentShrewd_C"],default="BP_ResidentShrewd_C")
    parser.add_argument("--port",type=int,default=18765)
    parser.add_argument("--seconds",type=int,default=22)
    parser.add_argument("--null-rhi",action="store_true")
    parser.add_argument("--dispatch-only",action="store_true",help="读到本轮玩家发言的 http_start 后即可结束宿主试验")
    parser.add_argument("--guide-format",action="store_true",help="独立诊断：在模型端限定首条回答格式，和原样运行的数据分别记录")
    parser.add_argument("--out",type=Path,required=True)
    options = parser.parse_args()
    results = run(options)
    options.out.parent.mkdir(parents=True,exist_ok=True)
    options.out.write_text(json.dumps(results,ensure_ascii=False,indent=2)+"\n",encoding="utf8")
    print(options.out)

