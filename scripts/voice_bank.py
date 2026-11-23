"""读取项目里的音色与录音。Forge 的试听和语音服务使用同一份台词表。"""
import json
import wave
from pathlib import Path


PROJECT = Path(__file__).resolve().parents[1]
if (PROJECT/"RedflagForge/assets").is_dir():
    PROJECT = PROJECT/"RedflagForge"


def load_bank(name, project=PROJECT):
    if not isinstance(name,str) or not name.endswith(".voice.json"):
        return None
    assets = (project/"assets").resolve()
    bank = (project/name).resolve()
    if not bank.is_relative_to(assets):
        raise ValueError("音色文件需要位于项目的 assets 目录。")
    return bank, json.loads(bank.read_bytes())


def recording(bank, text):
    path, voice = bank
    name = voice["Lines"].get(text)
    if not name:
        return None
    wav = (path.parent/name).resolve()
    if not wav.is_relative_to(path.parent) or wav.suffix.lower() != ".wav":
        raise ValueError("台词的 WAV 需要位于音色文件所在目录。")
    return wav


def read_pcm(path):
    with wave.open(str(path),"rb") as audio:
        if (audio.getnchannels(),audio.getsampwidth(),audio.getframerate()) != (1,2,16000):
            raise ValueError("游戏语音使用 16 kHz、16 位单声道 WAV。")
        return audio.readframes(audio.getnframes())
