"""Forge 把一次编辑写入临时文件，本程序用与 MCP、表格相同的校验保存，并返回结果。"""
import json
import os
import sys
from pathlib import Path
from urllib.request import Request, urlopen

from ruamel.yaml import YAML
import npc_assets
import npc_table
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from voice_bank import PROJECT, load_bank, recording


def execute(request):
    if request["operation"] == "exposure":
        from render_settings import apply_exposure
        return apply_exposure(Path(request["profile"]), request["original"], request["value"])
    profile = Path(request["profile"]).resolve()
    npc_assets.ASSETS = profile.parent
    cmd = request["operation"]
    if cmd == "transition":
        import animation_transitions
        check = animation_transitions.apply(profile, request["from"], request["to"], request["original"], request["value"])
        return {"profile": str(profile), **check}
    elif cmd == "create":
        npc_assets.create(profile.name, request["name"])
    elif cmd == "save":
        npc_assets.update(profile.name, request["changes"], request["original"])
    elif cmd == "states":
        npc_assets.edit_states(profile.name, request["changes"], request["original"])
    elif cmd == "new-animator":
        npc_assets.new_animator(profile.name, request["clip"])
    elif cmd == "copy":
        profile = profile.with_name(request["file"])
        npc_assets.copy(request["source"], profile.name, request["name"])
    elif cmd in ("export", "apply"):
        getattr(npc_table, cmd)(profile.name, request["table"], Path(request["file"]))
    elif cmd == "voices":
        port = int(os.environ.get("REDFLAG_SPEECH_PORT", 8717))
        with urlopen(f"http://127.0.0.1:{port}/v1/speech/voices", timeout=6) as response:
            return json.load(response)
    elif cmd == "speak":
        root = next((directory.parent for directory in profile.parents if directory.name=="assets"),PROJECT)
        bank = load_bank(request["voice"],root)
        fixed = recording(bank,request["text"]) if bank else None
        if fixed:
            Path(request["audio"]).write_bytes(fixed.read_bytes())
            return {"profile":str(profile)}
        data = json.dumps({"text": request["text"], "voice": request["voice"]}).encode()
        port = int(os.environ.get("REDFLAG_SPEECH_PORT", 8717))
        query = Request(f"http://127.0.0.1:{port}/v1/speech/synthesize", data, {"Content-Type": "application/json"})
        with urlopen(query, timeout=90 if bank else 20) as response:
            wave = response.read()
        if wave[:4] != b"RIFF" or wave[8:12] != b"WAVE":
            raise ValueError("语音服务未返回 WAV 音频。")
        Path(request["audio"]).write_bytes(wave)
    else:
        raise ValueError("未知的角色编辑操作。")
    return {"profile": str(profile)}


if __name__ == "__main__":
    reply = Path(sys.argv[1])
    try:
        result = execute(YAML(typ="safe").load(reply.read_bytes()))
    except Exception as e:
        result = {"error": str(e)}
    reply.write_text(json.dumps(result, ensure_ascii=False), encoding="utf-8")
