"""把 Forge 的一次操作交给与命令行、MCP 共用的项目开发资料工具。"""
import json
import os
from pathlib import Path
import subprocess
import sys

from ruamel.yaml import YAML

CLI = Path(__file__).resolve().parents[1] / "dev_context.py"


def execute(request):
    op = request["operation"]
    task = request.get("task", "")
    if op in ("find", "show"):
        args = [op, request["query"]]
        if op == "find":
            args.extend(["--limit", "4"])
        if task:
            args.extend(["--task", task])
    elif op == "tasks":
        args = [op]
    elif op == "review":
        args = [op, task]
    elif op == "note":
        args = [op, task, request["text"]]
    else:
        raise ValueError("未知的开发资料操作")
    env = dict(os.environ, REDFLAG_PROJECT=request["project"])
    result = subprocess.run([sys.executable, "-X", "utf8", str(CLI), *args], cwd=CLI.parent.parent, env=env,
                            capture_output=True, encoding="utf8", timeout=90)
    if result.returncode:
        if result.stderr.strip():
            raise RuntimeError(result.stderr.strip())
        raise RuntimeError(json.loads(result.stdout).get("error", "开发资料工具未完成请求。"))
    return json.loads(result.stdout)


if __name__ == "__main__":
    out = Path(sys.argv[1])
    try:
        value = execute(YAML(typ="safe").load(out.read_bytes()))
    except (ValueError, KeyError, OSError, subprocess.SubprocessError, RuntimeError) as exc:
        value = {"error": str(exc)}
    out.write_text(json.dumps(value, ensure_ascii=False), encoding="utf8")
