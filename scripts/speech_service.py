"""Redflag 本机语音服务。提供录音转文字及双向 WebRTC 音轨。"""
import argparse
import asyncio
import base64
import json
from io import BytesIO
from fractions import Fraction
from time import monotonic
import wave
from pathlib import Path
import subprocess
import os
import re
import threading
import sys
from uuid import uuid4
from collections import deque

from aiohttp import web
from aiortc import AudioStreamTrack, RTCConfiguration, RTCPeerConnection, RTCSessionDescription
from aiortc.mediastreams import MediaStreamError
from av import AudioFrame, AudioResampler
from speech_runtime import PlayerSpeech, Recognizer, read_wav
from voice_bank import PROJECT, load_bank, recording, read_pcm


# ---- 回复音轨与打断 ----
def spoken_phrases(text, limit):
    for sentence in re.findall(r"[^。！？!?；;\n]+[。！？!?；;]*|[。！？!?；;]+", text):
        sentence = sentence.strip()
        while len(sentence) > limit:
            ends = [pause.end() for pause in re.finditer(r"[，、,:：\s]", sentence[:limit])]
            end = ends[-1] if ends else limit
            yield sentence[:end]
            sentence = sentence[end:]
        if sentence: yield sentence


class BankSpeech:
    def __init__(self, project):
        self.project = project
        self.provider = None

    def voice(self):
        if not self.provider:
            from qwen_speech import QwenVoice
            self.provider = QwenVoice(self.project)
        return self.provider

    async def generate(self, bank, text, emit=None):
        path = recording(bank,text)
        if path:
            return read_pcm(path)
        if not self.project:
            language = bank[1].get("FallbackLanguage")
            if language:
                return await windows_speech(text, language=language)
            raise RuntimeError("qwen_voice_runtime_not_configured")
        provider = self.voice()
        cancel = threading.Event()
        try:
            # 模型每次最多接收 120 字，音频也有时长限制。完整句子分开朗读，仍用同一个说话人。
            pcm = bytearray()
            # 已送进声卡的字不能重读。流式先在停顿处分句，无标点时每次至多读 18 个字符；
            # 它给单段 12 秒的声音上限留余量，离线整句仍沿用模型的 120 字输入限制。
            pending = deque(spoken_phrases(text, 18 if emit is not None else 120))
            while pending:
                line = pending.popleft()
                try:
                    if emit is None:
                        pcm.extend(await asyncio.to_thread(provider.speak, bank[1], line, cancel))
                    else:
                        partial = False
                        def receive(block):
                            nonlocal partial
                            partial = True
                            emit(block)
                        await asyncio.to_thread(provider.speak_stream, bank[1], line, cancel, receive)
                except RuntimeError as error:
                    if str(error) != "neural_tts_audio_too_long" or len(line) < 2 or (emit is not None and partial):
                        raise
                    # 某些长句即使字数合规，读出来也超过单段时长。在接近中间的停顿处重试。
                    half = len(line)//2
                    breaks = [m.end() for m in re.finditer(r"[，、,:：\s]", line[:-1])]
                    end = min(breaks, key=lambda i: abs(i-half)) if breaks else half
                    pending.extendleft(reversed([part for part in (line[:end].strip(), line[end:].strip()) if part]))
            return bytes(pcm)
        except asyncio.CancelledError:
            cancel.set()
            raise

    def close(self):
        if self.provider:
            self.provider.close()


async def synthesize(text, voice, banks):
    bank = load_bank(voice)
    if bank:
        return await banks.generate(bank,text)
    return await windows_speech(text, voice)


async def windows_speech(text, voice="", language=""):
    cmd = Path(__file__).with_name("speech_synthesize.ps1")
    proc = await asyncio.create_subprocess_exec("powershell.exe","-NoLogo","-NoProfile","-NonInteractive",
            "-ExecutionPolicy","Bypass","-File",str(cmd), stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        body = json.dumps({"text":text,"voice":voice,"language":language},ensure_ascii=False).encode()
        pcm, _ = await asyncio.wait_for(proc.communicate(base64.b64encode(body)),15)
        if proc.returncode: raise RuntimeError("synthesis_failed")
        return pcm
    finally:
        if proc.returncode is None:
            proc.kill()
            await proc.wait()


class VoiceTrack(AudioStreamTrack):
    def __init__(self, notify, banks):
        super().__init__()
        self.notify = notify
        self.banks = banks
        self.turn = None
        self.pending = None
        self.pcm = b""
        self.offset = 0

    def cancel(self):
        if self.pending: self.pending.cancel()
        self.pending = None
        self.pcm = b""
        if self.turn: self.notify("cancelled",turn=self.turn)
        self.turn = None

    def speak(self, turn, text, voice=""):
        self.cancel()
        self.turn = turn
        self.pending = asyncio.create_task(self.prepare(turn,text,voice))

    async def prepare(self, turn, text, voice):
        try:
            pcm = await synthesize(text,voice,self.banks)
            if self.turn == turn:
                self.pcm,self.offset = pcm,0
                self.notify("audio_start",turn=turn)
        except (RuntimeError, ValueError, OSError, TimeoutError):
            self.notify("error",turn=turn,reason="synthesis_failed")

    async def recv(self):
        # aiortc 的时钟每 20 ms 前进 160 个 8 kHz 采样。16 kHz 讲话音轨同一时段有 320 个采样。
        frame = await super().recv()
        result = AudioFrame(format="s16",layout="mono",samples=320)
        result.sample_rate, result.time_base, result.pts = 16000, Fraction(1,16000), frame.pts*2
        piece = self.pcm[self.offset:self.offset+640]
        result.planes[0].update(piece.ljust(640,b"\0"))
        if piece:
            self.offset += len(piece)
            if self.offset >= len(self.pcm):
                self.notify("audio_end",turn=self.turn)
                self.turn,self.pcm = None,b""
        return result


# ---- 玩家语音与收到的文字 ----
async def listen(track, recognizer, notify, player):
    resample = AudioResampler(format="s16",layout="mono",rate=16000)
    pending = bytearray()
    async def deliver(pcm):
        if pcm:
            text = await asyncio.to_thread(recognizer.transcribe,pcm)
            if text: notify("transcript",text=text)
    try:
        while True:
            for block in resample.resample(await track.recv()):
                pending.extend(bytes(block.planes[0])[:block.samples*2])
            while len(pending) >= 640:
                pcm = player.push(bytes(pending[:640]))
                del pending[:640]
                await deliver(pcm)
    except MediaStreamError:
        if not player.manual:
            await deliver(player.vad.finish())


def application(recognizer,qwen_project=None):
    app = web.Application(client_max_size=1_000_000)
    peers = set()
    rooms = {}
    banks = BankSpeech(qwen_project)
    streams = {}

    # ---- 游戏角色与浏览器房间 ----
    async def new_room(req):
        key = uuid4().hex
        rooms[key] = {"texts":deque(), "peer":None, "last_poll":monotonic()}
        return web.json_response({"room":key},status=201)

    async def room(req):
        key = req.match_info["room"]
        current = rooms.get(key)
        if current is None: raise web.HTTPNotFound()
        if req.method == "DELETE":
            del rooms[key]
            if current["peer"]: await current["peer"].close()
            return web.json_response({"closed":True})
        current["last_poll"] = monotonic()
        texts = list(current["texts"])
        current["texts"].clear()
        return web.json_response({"texts":texts})

    async def page(req):
        if req.match_info["room"] not in rooms: raise web.HTTPNotFound()
        return web.FileResponse(Path(__file__).with_name("speech_room.html"))

    async def transcribe(req):
        try:
            text = await asyncio.to_thread(recognizer.transcribe,read_wav(await req.read()))
            return web.json_response({"text":text})
        except (ValueError, EOFError, wave.Error) as error:
            return web.json_response({"error":str(error)},status=400)

    speaker_settings = {"bank_speech": 4, "asset_root": str(PROJECT.resolve()),
                        "qwen_project": str(Path(banks.project).resolve()) if banks.project else "",
                        "qwen_free_text_configured": banks.project is not None}

    async def health(req):
        return web.json_response(speaker_settings)

    async def voices(req):
        cmd = Path(__file__).with_name("speech_synthesize.ps1")
        proc = await asyncio.create_subprocess_exec("powershell.exe","-NoProfile","-NonInteractive","-ExecutionPolicy",
                "Bypass","-File",str(cmd),"-List",stdout=subprocess.PIPE,creationflags=subprocess.CREATE_NO_WINDOW)
        data, _ = await proc.communicate()
        available = json.loads(data.decode("utf-8-sig"))
        available["voices"].extend(p.relative_to(PROJECT).as_posix() for p in sorted((PROJECT/"assets/audio").rglob("*.voice.json")))
        available.update(speaker_settings)
        return web.json_response(available)

    async def audition(req):
        body = await req.json()
        if not isinstance(body.get("text"),str) or not 0 < len(body["text"]) <= 300:
            return web.json_response({"error":"invalid_speech"},status=400)
        try:
            pcm = await synthesize(body["text"],body.get("voice",""),banks)
        except (RuntimeError, ValueError, OSError) as error:
            return web.json_response({"error":str(error)},status=400)
        output = BytesIO()
        with wave.open(output,"wb") as result:
            result.setparams((1,2,16000,0,"NONE",""));result.writeframes(pcm)
        return web.Response(body=output.getvalue(),content_type="audio/wav")

    # 游戏线程一直可以操作角色。合成线程每有一块 PCM 就交给这次请求，客户端只读自己的编号。
    async def start_stream(req):
        body = await req.json()
        if not isinstance(body, dict): return web.json_response({"error":"invalid_speech"}, status=400)
        text, voice = body.get("text"), body.get("voice", "")
        if not isinstance(text, str) or not 0 < len(text) <= 300 or not isinstance(voice, str):
            return web.json_response({"error":"invalid_speech"}, status=400)
        key = uuid4().hex
        queue = asyncio.Queue()
        loop = asyncio.get_running_loop()
        async def produce():
            try:
                bank = load_bank(voice)
                if bank:
                    pcm = await banks.generate(bank, text, lambda block: loop.call_soon_threadsafe(queue.put_nowait, block))
                else:
                    pcm = await windows_speech(text, voice)
                if pcm: queue.put_nowait(pcm)
                queue.put_nowait(None)
            except Exception as error:
                # 请求已经发给客户端。合成器报错也要交回终态，避免播放器一直等下一块。
                queue.put_nowait(error)
        streams[key] = {"queue":queue, "task":asyncio.create_task(produce()), "read":monotonic()}
        return web.json_response({"id":key}, status=201)

    async def stream(req):
        key = req.match_info["id"]
        current = streams.get(key)
        if current is None: return web.json_response({"error":"unknown_speech"}, status=404)
        if req.method == "DELETE":
            streams.pop(key)["task"].cancel()
            return web.Response(status=204)
        current["read"] = monotonic()
        try:
            # 客户端可在等待第一块期间打断，最长等 100 毫秒就能重新检查取消操作。
            block = await asyncio.wait_for(current["queue"].get(), .1)
        except TimeoutError:
            return web.Response(status=202)
        if isinstance(block, bytes): return web.Response(body=block, content_type="application/octet-stream")
        streams.pop(key, None)
        if block is None: return web.Response(status=204)
        return web.json_response({"error":str(block)}, status=400)

    async def offer(req):
        desc = await req.json()
        if desc.get("type") != "offer" or not isinstance(desc.get("sdp"),str):
            return web.json_response({"error":"invalid_offer"},status=400)
        key = desc.get("room")
        room = rooms.get(key)
        if key and not room: raise web.HTTPNotFound()
        if room and room["peer"]: await room["peer"].close()
        peer = RTCPeerConnection(RTCConfiguration(iceServers=[]))
        peers.add(peer)
        if room: room["peer"] = peer
        channel = None
        tasks = set()
        def notify(kind, **fields):
            if kind == "transcript" and room and rooms.get(key) is room and room["peer"] is peer:
                room["texts"].append(fields["text"])
            if channel and channel.readyState == "open":
                channel.send(json.dumps(dict(type=kind,**fields),ensure_ascii=False))
        speaker = VoiceTrack(notify,banks)
        player = PlayerSpeech()
        peer.addTrack(speaker)

        @peer.on("datachannel")
        def events(opened):
            nonlocal channel
            channel = opened
            @channel.on("message")
            def handle(raw):
                try:
                    message = json.loads(raw)
                    if message["type"] in ("listen_start","listen_end"):
                        player.button(message["type"] == "listen_start")
                        return
                    turn = message["turn"]
                    if not isinstance(turn,str) or not turn: raise ValueError()
                    if message["type"] == "cancel":
                        if turn == speaker.turn: speaker.cancel()
                    elif message["type"] == "speak" and isinstance(message["text"],str) and 0 < len(message["text"]) <= 300:
                        speaker.speak(turn,message["text"],message.get("voice",""))
                    else: raise ValueError()
                except (TypeError,ValueError,KeyError):
                    notify("error",reason="invalid_voice_message")

        @peer.on("track")
        def incoming(track):
            if track.kind == "audio":
                task = asyncio.create_task(listen(track,recognizer,notify,player))
                tasks.add(task)

        @peer.on("connectionstatechange")
        async def changed():
            if peer.connectionState in ("failed","closed"):
                speaker.cancel()
                for task in tasks: task.cancel()
                await asyncio.gather(*tasks,return_exceptions=True)
                peers.discard(peer)
                if room and room["peer"] is peer: room["peer"] = None
                if peer.connectionState != "closed": await peer.close()

        try:
            await peer.setRemoteDescription(RTCSessionDescription(desc["sdp"],"offer"))
            await peer.setLocalDescription(await peer.createAnswer())
        except Exception:
            await peer.close()
            return web.json_response({"error":"invalid_offer"},status=400)
        return web.json_response({"type":"answer","sdp":peer.localDescription.sdp})

    async def cleanup(_):
        workers = [item["task"] for item in streams.values()]
        for worker in workers: worker.cancel()
        await asyncio.gather(*workers, return_exceptions=True)
        await asyncio.gather(*(peer.close() for peer in list(peers)))
        await asyncio.to_thread(recognizer.close)
        await asyncio.to_thread(banks.close)

    async def room_lifetime(_):
        # 游戏每 200 ms 取一次新文字。进程异常退出满一分钟后，关闭仍挂在浏览器里的麦克风连接。
        async def expire():
            while True:
                await asyncio.sleep(15)
                cutoff = monotonic() - 60
                for key, current in list(streams.items()):
                    if current["read"] < cutoff: streams.pop(key)["task"].cancel()
                for key in list(rooms):
                    current = rooms.get(key)
                    if current and current["last_poll"] < cutoff:
                        del rooms[key]
                        if current["peer"]: await current["peer"].close()
        worker = asyncio.create_task(expire())
        yield
        worker.cancel()
        await asyncio.gather(worker,return_exceptions=True)

    async def warm_voice(_):
        cancel = threading.Event()
        async def prepare():
            try:
                await asyncio.to_thread(banks.voice().prepare, cancel)
            except (RuntimeError, ValueError, OSError, ImportError) as error:
                print(f"角色语音预加载失败：{error}", file=sys.stderr, flush=True)

        # 载入场景时就开始准备音色。已录好的台词可以立即播放。
        worker = asyncio.create_task(prepare())
        yield
        cancel.set()
        await worker

    if qwen_project:
        app.cleanup_ctx.append(warm_voice)
    app.router.add_post("/v1/speech/transcribe",transcribe)
    app.router.add_get("/v1/speech/health",health)
    app.router.add_get("/v1/speech/voices",voices)
    app.router.add_post("/v1/speech/synthesize",audition)
    app.router.add_post("/v1/speech/streams",start_stream)
    app.router.add_get("/v1/speech/streams/{id}",stream)
    app.router.add_delete("/v1/speech/streams/{id}",stream)
    app.router.add_post("/v1/speech/offer",offer)
    app.router.add_post("/v1/speech/rooms",new_room)
    app.router.add_get("/v1/speech/rooms/{room}",room)
    app.router.add_delete("/v1/speech/rooms/{room}",room)
    app.router.add_get("/room/{room}",page)
    app.on_cleanup.append(cleanup)
    app.cleanup_ctx.append(room_lifetime)
    return app


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime",type=Path,required=True,help="含 DeliverySpeechRuntime.dll 与 models 的目录")
    parser.add_argument("--port",type=int,default=8717)
    parser.add_argument("--qwen-project",type=Path,default=os.getenv("REDFLAG_QWEN_TTS_PROJECT"),
                        help="可选的 Delivery Qwen CustomVoice 安装目录，用于自由文本")
    args = parser.parse_args()
    web.run_app(application(Recognizer(args.runtime),args.qwen_project),host="127.0.0.1",port=args.port)
