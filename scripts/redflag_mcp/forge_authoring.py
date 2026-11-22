"""Forge 把一次编辑写入临时文件，本程序用与 MCP、表格相同的校验保存，并返回结果。"""
import json
import sys
from pathlib import Path
from urllib.request import Request, urlopen

from ruamel.yaml import YAML
import npc_assets
import npc_table


def execute(request):
    profile = Path(request["profile"]).resolve()
    npc_assets.ASSETS = profile.parent
    cmd = request["operation"]
    if cmd == "save":
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
        with urlopen("http://127.0.0.1:8717/v1/speech/voices", timeout=6) as response:
            return json.load(response)
    elif cmd == "speak":
        data = json.dumps({"text": request["text"], "voice": request["voice"]}).encode()
        query = Request("http://127.0.0.1:8717/v1/speech/synthesize", data, {"Content-Type": "application/json"})
        with urlopen(query, timeout=20) as response:
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
