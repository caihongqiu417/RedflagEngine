"""从 Forge 后台接收一次交接或批注，使用与 MCP 相同的命令编号和写入锁。"""
import json
import sys
from pathlib import Path
from ruamel.yaml import YAML

import playtest
import asset_changes


def execute(request):
    # “001”“true”也可能是玩家写的文字。只在协议指定的数字和开关字段上转换类型。
    if request["action"] == "asset-change":
        return asset_changes.apply(Path(request["project"]), request["change"], request["undo"] == "true")
    folder = playtest.run_folder(Path(request["project"]), request["session"])
    if request["action"] == "feedback":
        return playtest.feedback(folder, int(request["seq"]), request["entity"], request["text"])
    if request["action"] == "handoff":
        return playtest.issue(folder, int(request["generation"]), "handoff", owner=request["owner"])
    raise ValueError("未知的试玩操作。")


if __name__ == "__main__":
    reply = Path(sys.argv[1])
    try:
        result = execute(YAML(typ="base").load(reply.read_bytes()))
    except (ValueError, OSError, KeyError) as error:
        result = {"error": str(error)}
    reply.write_text(json.dumps(result, ensure_ascii=False), encoding="utf8")
