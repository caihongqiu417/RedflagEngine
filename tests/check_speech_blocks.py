"""在仓库根目录运行。使用受控的两块 PCM 验证正式 HTTP、Windows 播放设备、打断与回听录音，不加载 GPU 模型。"""
import asyncio
from io import BytesIO
from pathlib import Path
import sys
import threading
import wave
from unittest.mock import patch

from aiohttp import ClientSession, web

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/"scripts"))
from qwen_speech import QwenVoice
from speech_service import BankSpeech, application


class Source:
    def __init__(self):
        self.release = threading.Event()
        self.cancelled = threading.Event()
        self.lock = threading.Lock()
        self.spoken = []

    def prepare(self, cancel): pass
    def close(self): pass

    def speak_stream(self, voice, line, cancel, emit):
        with self.lock:
            self.spoken.append(line)
            emit(b"\x91\x01"*4800)
            if line == "测试分块播放":
                if not self.release.wait(8): raise RuntimeError("player_did_not_receive_first_chunk")
            if line == "请求会一直等到打断":
                if not cancel.wait(8): raise RuntimeError("stream_did_not_cancel")
                self.cancelled.set()
                return
            if line == "声音中途出错": raise RuntimeError("neural_tts_audio_too_long")
            emit(b"\x43\x01"*4800)


def check_resampler():
    class Synth:
        identities = {"role":{"speaker":"voice", "model_revision":"r"}}
        voice_name = "voice"
        supports_audio_stream = True
        raw = b"\xaf\x0f\x51\xfa"*20000
        def prepare(self, cancel): pass
        def synthesize(self, *args): return self.raw, 24000
        def synthesize_stream(self, text, voice, mood, cancel, emit, chunk_frames):
            for pos in range(0, len(self.raw), 3840): emit(self.raw[pos:pos+3840])
            return len(self.raw), 24000

    provider = QwenVoice.__new__(QwenVoice)
    provider.synth = Synth()
    role = {"Profile":"role", "Speaker":"voice", "Revision":"r"}
    expected = provider.speak(role, "样音", threading.Event())
    for supported in (True, False):
        blocks = []
        provider.synth.supports_audio_stream = supported
        provider.speak_stream(role, "样音", threading.Event(), blocks.append)
        assert b"".join(blocks) == expected
        assert (len(blocks) > 1) == supported
    print("PASS continuous 24→16 kHz samples equal whole WAV; non-stream provider keeps voice")


async def run():
    source = Source()
    with patch.object(BankSpeech, "voice", return_value=source):
        app = application(source, ROOT)
        async def release(req):
            source.release.set()
            return web.json_response({"released":True})
        async def status(req):
            return web.json_response({"cancelled":source.cancelled.is_set()})
        app.router.add_post("/test/continue", release)
        app.router.add_get("/test/status", status)
        runner = web.AppRunner(app)
        await runner.setup()
        await web.TCPSite(runner, "127.0.0.1", 0).start()
        port = runner.addresses[0][1]
        try:
            process = await asyncio.create_subprocess_exec(str(ROOT/"bin-int/Tests/Decision/SpeechPlaybackTests.exe"),
                str(port), "--chunks", cwd=ROOT/"RedflagForge")
            assert await asyncio.wait_for(process.wait(), 32) == 0
            assert source.spoken.count("声音中途出错") == 1
            async with ClientSession() as http:
                url = f"http://127.0.0.1:{port}/v1/speech/streams"
                for text in ("你刚才跟我提过维修站的事情。走到厂房门口时，咱们可以停一停，先听听里面还有没有声音，再一起看看地上散落的零件都放在哪里。",
                             "脚步声来自厂房里面还是钢梁后面我们还没有走到屋顶所以暂时看不清楚我们走近以后再确认"*5):
                    source.spoken.clear()
                    response = await http.post(url, json={"text":text,"voice":"assets/audio/watt/dylan.voice.json"})
                    response.raise_for_status()
                    path = url + "/" + (await response.json())["id"]
                    size = 0
                    while True:
                        response = await http.get(path)
                        response.raise_for_status()
                        if response.status == 204: break
                        size += len(await response.read())
                    assert "".join(source.spoken) == text and max(map(len, source.spoken)) <= 18
                    assert size == len(source.spoken)*19200
                    print(f"PASS {len(text)} characters spoken exactly once in {len(source.spoken)} parts")
        finally:
            await runner.cleanup()

    # 旧服务没有可轮询的请求编号。客户端只在接口缺失时回退，损坏的 WAV 会留下明确错误。
    legacy = web.Application()
    calls = []
    async def wav(req):
        text = (await req.json())["text"]
        calls.append(text)
        if text == "声音文件损坏": return web.Response(body=b"broken")
        audio = BytesIO()
        with wave.open(audio, "wb") as file:
            file.setparams((1,2,16000,0,"NONE",""))
            file.writeframes(b"\x91\x01"*4800)
        return web.Response(body=audio.getvalue(), content_type="audio/wav")
    legacy.router.add_post("/v1/speech/synthesize", wav)
    runner = web.AppRunner(legacy)
    await runner.setup()
    await web.TCPSite(runner, "127.0.0.1", 0).start()
    try:
        child = await asyncio.create_subprocess_exec(str(ROOT/"bin-int/Tests/Decision/SpeechPlaybackTests.exe"),
            str(runner.addresses[0][1]), "--legacy", cwd=ROOT/"RedflagForge")
        assert await asyncio.wait_for(child.wait(), 32) == 0
        assert calls == ["旧服务整句返回", "声音文件损坏"]
    finally:
        await runner.cleanup()


if __name__ == "__main__":
    check_resampler()
    asyncio.run(run())
