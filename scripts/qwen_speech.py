"""复用本机安装的 Delivery CustomVoice 配置，让 Redflag 运行与烘焙使用同一个说话人。"""
import argparse
import hashlib
import json
import sys
import threading
import time
import wave
from pathlib import Path

from av import AudioFrame, AudioResampler


class QwenVoice:
    def __init__(self, project):
        tools = Path(project).resolve() / "Tools/DeliveryAI"
        if not (tools/"neural_tts.py").is_file():
            raise ValueError("找不到 Delivery 的 Qwen 语音安装。")
        sys.path.insert(0, str(tools))
        from neural_tts import QwenSpeechSynthesizer
        self.synth = QwenSpeechSynthesizer()

    def prepare(self, cancel):
        self.synth.prepare(cancel)

    def profile(self, voice):
        profile, speaker = voice["Profile"], voice["Speaker"]
        identity = self.synth.identities[profile]
        if identity["speaker"] != speaker or identity["model_revision"] != voice["Revision"]:
            raise ValueError("本机模型的版本或说话人与音色文件不一致。")
        return profile

    def speak(self, voice, text, cancel):
        profile = self.profile(voice)
        pcm, rate = self.synth.synthesize(text, profile, "Calm", cancel)
        if self.synth.voice_name != voice["Speaker"]:
            raise ValueError("合成器返回了其他说话人的结果。")
        convert = AudioResampler(format="s16", layout="mono", rate=16000)
        return resample(convert, pcm, rate) + resample(convert, None, rate)

    def speak_stream(self, voice, text, cancel, emit):
        profile = self.profile(voice)
        self.synth.prepare(cancel)
        if not getattr(self.synth, "supports_audio_stream", False):
            emit(self.speak(voice, text, cancel))
            return

        # 一句话共用同一个滤波器，块与块之间保留重采样所需的少量相邻采样。
        convert = AudioResampler(format="s16", layout="mono", rate=16000)
        def receive(pcm):
            block = resample(convert, pcm, 24000)
            if block and not cancel.is_set(): emit(block)
        self.synth.synthesize_stream(text, profile, "Calm", cancel, receive, chunk_frames=8)
        if self.synth.voice_name != voice["Speaker"]:
            raise ValueError("合成器返回了其他说话人的结果。")
        tail = resample(convert, None, 24000)
        if tail and not cancel.is_set(): emit(tail)

    def close(self):
        self.synth.close()


def resample(convert, pcm, rate):
    frame = None
    if pcm is not None:
        frame = AudioFrame(format="s16", layout="mono", samples=len(pcm)//2)
        frame.sample_rate = rate
        frame.planes[0].update(pcm)
    return b"".join(bytes(block.planes[0])[:block.samples*2] for block in convert.resample(frame))


def bake(project, bank, report):
    voice = json.loads(bank.read_bytes())
    result = {"model":voice["Model"], "revision":voice["Revision"], "profile":voice["Profile"],
              "speaker":voice["Speaker"], "rate_hz":16000, "lines":[]}
    provider = QwenVoice(project)
    try:
        for text, name in voice["Lines"].items():
            start = time.monotonic()
            pcm = provider.speak(voice,text,threading.Event())
            target = bank.parent/name
            with wave.open(str(target),"wb") as sound:
                sound.setparams((1,2,16000,0,"NONE",""))
                sound.writeframes(pcm)
            row = dict(text=text, file=name, speaker=provider.synth.voice_name, samples=len(pcm)//2,
                       sha256=hashlib.sha256(target.read_bytes()).hexdigest(), seconds=round(time.monotonic()-start,3))
            result["lines"].append(row)
            print(name, row["speaker"], row["samples"], flush=True)
    finally:
        provider.close()
    report.parent.mkdir(parents=True,exist_ok=True)
    report.write_text(json.dumps(result,ensure_ascii=False,indent=2)+"\n", encoding="utf8")


if __name__ == "__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--delivery",required=True,type=Path)
    parser.add_argument("--voice",type=Path,default=Path("RedflagForge/assets/audio/watt/dylan.voice.json"))
    parser.add_argument("--report",type=Path,default=Path("output/validation/watt-voice/bake.json"))
    args=parser.parse_args()
    bake(args.delivery,args.voice,args.report)
