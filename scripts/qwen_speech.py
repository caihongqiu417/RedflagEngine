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

    def speak(self, voice, text, cancel):
        profile, speaker = voice["Profile"], voice["Speaker"]
        identity = self.synth.identities[profile]
        if identity["speaker"] != speaker or identity["model_revision"] != voice["Revision"]:
            raise ValueError("本机模型的版本或说话人与音色文件不一致。")
        pcm, rate = self.synth.synthesize(text, profile, "Calm", cancel)
        if self.synth.voice_name != speaker:
            raise ValueError("合成器返回了其他说话人的结果。")

        # Qwen 输出 24 kHz。用带低通滤波的重采样器转换到游戏与 WebRTC 共用的 16 kHz。
        frame = AudioFrame(format="s16", layout="mono", samples=len(pcm)//2)
        frame.sample_rate = rate
        frame.planes[0].update(pcm)
        convert = AudioResampler(format="s16", layout="mono", rate=16000)
        blocks = convert.resample(frame) + convert.resample(None)
        return b"".join(bytes(block.planes[0])[:block.samples*2] for block in blocks)

    def close(self):
        self.synth.close()


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
