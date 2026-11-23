"""通过正式 HTTP 合成接口核对瓦特的录音，并试一句台词表中没有的中文。"""
import argparse
import asyncio
import hashlib
import json
import sys
import wave
from io import BytesIO
from pathlib import Path

from aiohttp import ClientSession, web

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"scripts"))
from speech_service import application
from speech_runtime import Recognizer


async def requests(runtime,delivery,output):
    voice = "assets/audio/watt/dylan.voice.json"
    lines = json.loads((ROOT/"RedflagForge"/voice).read_bytes())["Lines"]
    prompt = "钢板已经固定，可以继续检查左侧的管线了。"
    assert prompt not in lines
    output.mkdir(parents=True,exist_ok=True)
    runner=web.AppRunner(application(Recognizer(runtime),delivery))
    await runner.setup()
    await web.TCPSite(runner,"127.0.0.1",0).start()
    base="http://127.0.0.1:"+str(runner.addresses[0][1])+"/v1/speech/"
    try:
        async with ClientSession() as http:
            async with http.get(base+"voices") as response:
                response.raise_for_status()
                voices=await response.json()
            assert voice in voices["voices"] and voices["default_voice"] in voices["voices"]
            async def read(text,speaker):
                async with http.post(base+"synthesize",json={"text":text,"voice":speaker}) as response:
                    data=await response.read()
                    if response.status!=200: raise RuntimeError(data.decode())
                    with wave.open(BytesIO(data)) as audio:
                        assert (audio.getnchannels(),audio.getsampwidth(),audio.getframerate())==(1,2,16000)
                        assert audio.getnframes()>1600
                    return data
            preview="我在这里等你。"
            wav=await read(preview,voice)
            assert wav==(ROOT/"RedflagForge"/Path(voice).parent/lines[preview]).read_bytes()
            sapi=await read(preview,voices["default_voice"])
            assert sapi!=wav
            spoken=await read(prompt,voice)
            (output/"free-text.wav").write_bytes(spoken)
            return {"sapi_default":voices["default_voice"],"voices":voices["voices"],"offline_preview_sha256":hashlib.sha256(wav).hexdigest(),
                    "same_text_sapi_sha256":hashlib.sha256(sapi).hexdigest(),"free_text":prompt,
                    "free_text_sha256":hashlib.sha256(spoken).hexdigest(),"free_text_samples":(len(spoken)-44)//2}
    finally:
        await runner.cleanup()


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime",type=Path,required=True)
    parser.add_argument("--delivery",type=Path,required=True)
    parser.add_argument("--output",type=Path,default=ROOT/"output/validation/watt-voice")
    args=parser.parse_args()
    result=asyncio.run(requests(args.runtime,args.delivery,args.output))
    (args.output/"service.json").write_text(json.dumps(result,ensure_ascii=False,indent=2)+"\n",encoding="utf8")
    print(result)
