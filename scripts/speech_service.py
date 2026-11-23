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
import threading
from uuid import uuid4
from collections import deque

from aiohttp import web
from aiortc import AudioStreamTrack, RTCConfiguration, RTCPeerConnection, RTCSessionDescription
from aiortc.mediastreams import MediaStreamError
from av import AudioFrame, AudioResampler
from speech_runtime import PlayerSpeech, Recognizer, read_wav
from voice_bank import PROJECT, load_bank, recording, read_pcm


# ---- 回复音轨与打断 ----
class BankSpeech:
    def __init__(self, project):
        self.project = project
        self.provider = None

    async def generate(self, bank, text):
        path = recording(bank,text)
        if path:
            return read_pcm(path)
        if not self.project:
            raise RuntimeError("qwen_voice_runtime_not_configured")
        if len(text)>120:
            raise ValueError("qwen_voice_limit_120_characters")
        if not self.provider:
            from qwen_speech import QwenVoice
            self.provider = QwenVoice(self.project)
        cancel = threading.Event()
        try:
            return await asyncio.to_thread(self.provider.speak,bank[1],text,cancel)
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
    cmd = Path(__file__).with_name("speech_synthesize.ps1")
    proc = await asyncio.create_subprocess_exec("powershell.exe","-NoLogo","-NoProfile","-NonInteractive",
            "-ExecutionPolicy","Bypass","-File",str(cmd), stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        body = json.dumps({"text":text,"voice":voice},ensure_ascii=False).encode()
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

    async def voices(req):
        cmd = Path(__file__).with_name("speech_synthesize.ps1")
        proc = await asyncio.create_subprocess_exec("powershell.exe","-NoProfile","-NonInteractive","-ExecutionPolicy",
                "Bypass","-File",str(cmd),"-List",stdout=subprocess.PIPE,creationflags=subprocess.CREATE_NO_WINDOW)
        data, _ = await proc.communicate()
        available = json.loads(data.decode("utf-8-sig"))
        available["voices"].extend(p.relative_to(PROJECT).as_posix() for p in sorted((PROJECT/"assets/audio").rglob("*.voice.json")))
        available["qwen_free_text_configured"] = banks.project is not None
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
        await asyncio.gather(*(peer.close() for peer in list(peers)))
        await asyncio.to_thread(recognizer.close)
        await asyncio.to_thread(banks.close)

    async def room_lifetime(_):
        # 游戏每 200 ms 取一次新文字。进程异常退出满一分钟后，关闭仍挂在浏览器里的麦克风连接。
        async def expire():
            while True:
                await asyncio.sleep(15)
                cutoff = monotonic() - 60
                for key in list(rooms):
                    current = rooms.get(key)
                    if current and current["last_poll"] < cutoff:
                        del rooms[key]
                        if current["peer"]: await current["peer"].close()
        worker = asyncio.create_task(expire())
        yield
        worker.cancel()
        await asyncio.gather(worker,return_exceptions=True)

    app.router.add_post("/v1/speech/transcribe",transcribe)
    app.router.add_get("/v1/speech/voices",voices)
    app.router.add_post("/v1/speech/synthesize",audition)
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
