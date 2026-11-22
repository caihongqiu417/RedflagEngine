"""用公开普通话录音贯通 WebRTC、VAD、中文识别和回复音轨。测试不会打开麦克风或扬声器。"""
import argparse
import asyncio
from array import array
from fractions import Fraction
import hashlib
import json
from pathlib import Path
import sys
import time

from aiohttp import ClientSession, web
from aiortc import AudioStreamTrack
from av import AudioFrame, AudioResampler

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"scripts"))
from speech_client import SpeechSession
from speech_runtime import Recognizer, Utterance, read_wav
from speech_service import application


class Recording(AudioStreamTrack):
    def __init__(self, pcm):
        super().__init__()
        self.pcm,self.cursor = pcm,0
        self.started = False

    async def recv(self):
        clock = await super().recv()
        frame = AudioFrame(format="s16",layout="mono",samples=320)
        frame.sample_rate,frame.time_base,frame.pts = 16000,Fraction(1,16000),clock.pts*2
        chunk = self.pcm[self.cursor:self.cursor+640] if self.started else b""
        if self.started: self.cursor += 640
        frame.planes[0].update(chunk.ljust(640,b"\0"))
        return frame


async def run(args):
    wave = args.wav.read_bytes()
    pcm = read_wav(wave)
    runner = web.AppRunner(application(Recognizer(args.runtime)))
    await runner.setup()
    site = web.TCPSite(runner,"127.0.0.1",0)
    await site.start()
    url = "http://127.0.0.1:" + str(runner.addresses[0][1])
    microphone = Recording(pcm)
    events, volume = [],[]
    start = time.monotonic()
    def event(message):
        events.append(dict(at_ms=round((time.monotonic()-start)*1000),**message))
    decoded = AudioResampler(format="s16",layout="mono",rate=16000)
    async def audio(frame):
        for block in decoded.resample(frame):
            samples = array("h",bytes(block.planes[0])[:block.samples*2])
            rms = (sum(v*v for v in samples)/len(samples))**.5/32768
            volume.append((time.monotonic(),rms))
    session = SpeechSession(event,audio)
    async def until(check,seconds=15):
        async with asyncio.timeout(seconds):
            while not check(): await asyncio.sleep(.025)
    def seen(name,turn):
        return any(e.get("type") == name and e.get("turn") == turn for e in events)
    try:
        async with ClientSession() as http:
            async with http.post(url+"/v1/speech/transcribe",data=wave) as reply:
                reply.raise_for_status()
                direct = (await reply.json())["text"]
            async with http.post(url+"/v1/speech/rooms") as reply:
                room = (await reply.json())["room"]
            async with http.post(url+"/v1/speech/rooms") as reply:
                other_room = (await reply.json())["room"]
        await session.connect(url,microphone,room)
        session.hold(True)
        microphone.started = True
        await until(lambda: microphone.cursor >= len(pcm)+3200,12)
        session.hold(False)
        def spoken():
            return "".join(e["text"] for e in events if e["type"] == "transcript")
        await until(lambda: "法国" in spoken() and "演员" in spoken(),22)
        session.speak("one","Please bring me the key to the cabinet. I will wait for you at the front desk.")
        await until(lambda: seen("audio_start","one") and any(v>.005 for _,v in volume))
        session.cancel("one")
        await until(lambda: seen("cancelled","one"))
        cancelled = time.monotonic()
        await asyncio.sleep(.7)
        tail = [v for t,v in volume if t>cancelled+.4]
        assert tail and max(tail) < .002 and not seen("audio_end","one"), tail
        session.speak("two","Thank you.")
        await until(lambda: seen("audio_end","two"))
        assert "法国" in direct and "演员" in direct
        async with ClientSession() as http:
            address = url+"/v1/speech/rooms/"
            async with http.get(address+room) as reply:
                delivered = (await reply.json())["texts"]
            assert delivered == [spoken()]
            for key in (room, other_room):
                async with http.get(address+key) as reply:
                    assert (await reply.json())["texts"] == []
                async with http.delete(address+key) as reply:
                    assert reply.status == 200
                async with http.get(address+key) as reply:
                    assert reply.status == 404
        vad = Utterance()
        blocks = [pcm[i:i+640].ljust(640,b"\0") for i in range(0,len(pcm),640)] + [bytes(640)]*25
        phrases = [len(phrase)/32000 for block in blocks if (phrase := vad.push(block))]
        assert len(phrases) >= 2
        return dict(recording_sha256=hashlib.sha256(wave).hexdigest(),duration_seconds=len(pcm)/32000,
                    native_text=direct,rtc_text=spoken(),actor_room_texts=delivered,stale_room_status=404,
                    auto_vad_phrases_seconds=phrases,
                    audio_frames_received=len(volume),max_rms=max(v for _,v in volume),
                    max_tail_rms_400ms_after_cancel=max(tail),events=events)
    finally:
        await session.close()
        await runner.cleanup()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime",type=Path,required=True)
    parser.add_argument("--wav",type=Path,required=True)
    parser.add_argument("--out",type=Path,default=ROOT/"output/validation/runtime-ai/speech.json")
    args = parser.parse_args()
    report = asyncio.run(run(args))
    args.out.parent.mkdir(parents=True,exist_ok=True)
    args.out.write_text(json.dumps(report,indent=2,ensure_ascii=False)+"\n",encoding="utf8")
    print(report)
