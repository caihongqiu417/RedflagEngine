"""读取中文录音，合成 16 kHz 单声道回复。模型与系统音色由启动服务的机器提供。"""
from array import array
from collections import deque
import ctypes as c
from io import BytesIO
from pathlib import Path
import threading
import wave


class Config(c.Structure):
    _fields_ = [("size",c.c_uint32), ("abi",c.c_uint32), ("models",c.c_char_p), ("hotwords",c.c_char_p),
                ("threads",c.c_uint32), ("max_ms",c.c_uint32), ("score",c.c_float), ("reserved",c.c_uint32)]


class Result(c.Structure):
    _fields_ = [("size",c.c_uint32), ("state",c.c_uint32), ("turn",c.c_uint64), ("revision",c.c_uint64),
                ("samples",c.c_uint64), ("rate",c.c_int32), ("reserved",c.c_uint32)]


# ---- 中文语音识别 ----
class Recognizer:
    def __init__(self, runtime):
        runtime = Path(runtime).resolve()
        dll = c.CDLL(str(runtime / "DeliverySpeechRuntime.dll"))
        ptr, u32, u64 = c.c_void_p, c.c_uint32, c.c_uint64
        functions = {"dsr_abi_version":(u32,[]), "dsr_create":(c.c_int,[c.POINTER(Config),c.POINTER(ptr),c.c_char_p,u32]),
                     "dsr_begin":(c.c_int,[ptr,u64]), "dsr_push":(c.c_int,[ptr,u64,c.c_int,c.POINTER(c.c_float),u32]),
                     "dsr_finish":(c.c_int,[ptr,u64]), "dsr_cancel":(c.c_int,[ptr,u64]), "dsr_destroy":(None,[ptr]),
                     "dsr_get_result":(c.c_int,[ptr,c.POINTER(Result),c.c_char_p,u32,c.POINTER(u32)])}
        for name,(returns,args) in functions.items():
            call = getattr(dll,name)
            call.restype, call.argtypes = returns,args
        if dll.dsr_abi_version() != 1:
            raise ValueError("unsupported_recognizer_abi")
        cfg = Config(c.sizeof(Config),1,str(runtime/"models").encode(),b"",1,15000,0,0)
        self.handle = ptr()
        error = c.create_string_buffer(2048)
        if dll.dsr_create(c.byref(cfg),c.byref(self.handle),error,len(error)):
            raise RuntimeError("recognizer_initialization_failed")
        self.dll, self.turn, self.lock = dll, 0, threading.Lock()

    def transcribe(self, pcm):
        """一轮最多 15 秒。共享模型在工作线程上串行解码，各轮使用独立编号。"""
        if not pcm or len(pcm)%2 or len(pcm) > 16000*2*15:
            raise ValueError("invalid_clip_length")
        samples = array("h",pcm)
        with self.lock:
            self.turn += 1
            turn, dll, h = self.turn,self.dll,self.handle
            if dll.dsr_begin(h,turn): raise RuntimeError("recognizer_begin_failed")
            try:
                for start in range(0,len(samples),1600):
                    chunk = samples[start:start+1600]
                    data = (c.c_float*len(chunk))(*(v / 32768 for v in chunk))
                    if dll.dsr_push(h,turn,16000,data,len(data)): raise RuntimeError("recognizer_audio_failed")
                if dll.dsr_finish(h,turn): raise RuntimeError("recognizer_finish_failed")
                info, size = Result(size=c.sizeof(Result)), c.c_uint32()
                if dll.dsr_get_result(h,c.byref(info),None,0,c.byref(size)) != 4:
                    raise RuntimeError("recognizer_text_failed")
                text = c.create_string_buffer(size.value)
                if dll.dsr_get_result(h,c.byref(info),text,len(text),c.byref(size)):
                    raise RuntimeError("recognizer_text_failed")
                return text.value.decode()
            except Exception:
                dll.dsr_cancel(h,turn)
                raise

    def close(self):
        with self.lock:
            self.dll.dsr_destroy(self.handle)


# ---- 语音起止与录音格式 ----
class Utterance:
    def __init__(self):
        import webrtcvad
        self.vad = webrtcvad.Vad(2)
        self.lead = deque(maxlen=5)
        self.parts = []
        self.quiet = 0

    def push(self, frame):
        """每块 20ms。开头带回 100ms，句尾连续安静 400ms 后交给识别。"""
        voice = self.vad.is_speech(frame,16000)
        if not self.parts:
            if not voice:
                self.lead.append(frame)
                return None
            self.parts.extend(self.lead)
            self.lead.clear()
        self.parts.append(frame)
        self.quiet = 0 if voice else self.quiet + 1
        return self.finish() if self.quiet >= 20 or len(self.parts) >= 750 else None

    def finish(self):
        pcm = b"".join(self.parts)
        self.parts.clear()
        self.quiet = 0
        return pcm or None


class PlayerSpeech:
    def __init__(self):
        self.vad = Utterance()
        self.manual = False
        self.recording = None
        self.remaining = None

    def button(self, down):
        self.manual = True
        if down:
            self.recording = bytearray()
            self.remaining = None
        elif self.recording is not None:
            # 松键消息和声音分别传输，留 200 ms 给已经在途的最后几个音频包。
            self.remaining = 10

    def push(self, block):
        if not self.manual:
            return self.vad.push(block)
        if self.recording is not None:
            self.recording.extend(block)
            if self.remaining is not None:
                self.remaining -= 1
            if self.remaining == 0 or len(self.recording) >= 15*16000*2:
                result, self.recording = bytes(self.recording), None
                return result
        return None


def read_wav(blob):
    with wave.open(BytesIO(blob),"rb") as source:
        if (source.getnchannels(),source.getsampwidth(),source.getframerate()) != (1,2,16000):
            raise ValueError("expected_16000_mono_pcm16")
        return source.readframes(source.getnframes())
