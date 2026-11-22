"""用静音音轨连本机语音房。打断正在播放的一句话，检查接收端实际解码出来的样本。"""
import argparse
import asyncio
from array import array
import json
import sys
import time
from pathlib import Path
from aiortc import AudioStreamTrack
from av import AudioResampler

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/"scripts"))
from speech_client import SpeechSession


async def measure(count):
    volume, cancelled, trials = [], {}, []
    resampler = AudioResampler(format="s16", layout="mono", rate=16000)

    def event(message):
        if message["type"] == "cancelled": cancelled[message["turn"]] = time.perf_counter()

    async def audio(frame):
        for mono in resampler.resample(frame):
            samples = array("h", bytes(mono.planes[0])[:mono.samples*2])
            rms = (sum(v*v for v in samples) / len(samples))**.5 / 32768
            volume.append((time.perf_counter(), rms))

    session = SpeechSession(event, audio)
    try:
        await session.connect("http://127.0.0.1:8717", AudioStreamTrack())
        for trial in range(count):
            volume.clear()
            key = "sample-"+str(trial+1)
            session.speak(key,"这是测试播放的一句话，后半句应当在我打断它以后停下来。")
            # 连续收到三个有声块时发出打断，避免在两个字中间的短暂停顿计时。
            async with asyncio.timeout(10):
                while len(volume)<3 or min(v for _,v in volume[-3:]) < .005:
                    await asyncio.sleep(.005)
            start = time.perf_counter()
            session.cancel(key)
            await asyncio.sleep(.7)
            tail = [(t,v) for t,v in volume if t >= start]
            if not tail or key not in cancelled: raise RuntimeError("接收器没有音频块或取消回执")
            last = max((t for t,v in tail if v>=.002), default=start)
            quiet = next(t for t,v in tail if t>last and v<.002)
            late = [v for t,v in tail if t-start>=.4]
            if not late or max(late)>=.002: raise RuntimeError("400 毫秒后仍有声音")
            trials.append(dict(trial=trial+1,ack_ms=round((cancelled[key]-start)*1000,2),
                               last_loud_ms=round((last-start)*1000,2),quiet_ms=round((quiet-start)*1000,2),
                               max_rms_after_400ms=round(max(late),6)))
    finally:
        await session.close()
    return trials


if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--trials",type=int,default=20)
    p.add_argument("--out",type=Path,required=True)
    args=p.parse_args()
    args.out.write_text(json.dumps(dict(rms_threshold=.002,trials=asyncio.run(measure(args.trials))), indent=2)+"\n",encoding="utf8")
