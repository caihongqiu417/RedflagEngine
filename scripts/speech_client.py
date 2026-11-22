"""游戏工具与语音服务的媒体连接。on_event 收到 transcript 时，把其中的 text 交给游戏。"""
import asyncio
import json

from aiohttp import ClientSession
from aiortc import RTCConfiguration, RTCPeerConnection, RTCSessionDescription
from aiortc.mediastreams import MediaStreamError


class SpeechSession:
    def __init__(self, on_event, on_audio):
        self.on_event, self.on_audio = on_event,on_audio
        self.peer = RTCPeerConnection(RTCConfiguration(iceServers=[]))
        self.events = self.peer.createDataChannel("speech")
        self.ready = asyncio.Event()
        self.events.on("open",self.ready.set)
        self.events.on("message",lambda raw: self.on_event(json.loads(raw)))
        self.receiver = None

        @self.peer.on("track")
        def received(track):
            if track.kind == "audio":
                self.receiver = asyncio.create_task(self.read_audio(track))

    async def connect(self, endpoint, microphone, room=None):
        self.peer.addTrack(microphone)
        await self.peer.setLocalDescription(await self.peer.createOffer())
        async with ClientSession() as http:
            offer = self.peer.localDescription
            async with http.post(endpoint+"/v1/speech/offer",json={"type":offer.type,"sdp":offer.sdp,"room":room}) as reply:
                reply.raise_for_status()
                answer = await reply.json()
        await self.peer.setRemoteDescription(RTCSessionDescription(**answer))
        await asyncio.wait_for(self.ready.wait(),10)

    async def read_audio(self, track):
        try:
            while True:
                await self.on_audio(await track.recv())
        except MediaStreamError:
            pass

    def speak(self, turn, text, voice=""):
        self.events.send(json.dumps({"type":"speak","turn":turn,"text":text,"voice":voice},ensure_ascii=False))

    def cancel(self, turn):
        self.events.send(json.dumps({"type":"cancel","turn":turn}))

    def hold(self, pressed):
        self.events.send(json.dumps({"type":"listen_start" if pressed else "listen_end"}))

    async def close(self):
        await self.peer.close()
        if self.receiver: await self.receiver
