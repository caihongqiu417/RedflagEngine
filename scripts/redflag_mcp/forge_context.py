"""把 Forge 的一次操作交给与命令行、MCP 共用的项目开发资料工具。"""
import json
import os
from pathlib import Path
import subprocess
import sys

from ruamel.yaml import YAML

CLI = Path(__file__).resolve().parents[1] / "dev_context.py"
sys.path.insert(0, str(CLI.parent))
from context_index import read_text, source_path
from context_review import changes, folder


def document(request):
    """源码取自当前检出目录，日志和差异取自选中项目里的那次测试。"""
    root = CLI.parent.parent
    kind, name = request["kind"], request.get("file", "")
    if kind == "source":
        filename, sep, line = name.rpartition(":")
        _, path = source_path(root, filename if sep and line.isdigit() else name)
        focus = int(line) if sep and line.isdigit() else 1
        title = path.name
    else:
        directory = folder(Path(request["project"]).resolve(), request["task"]).resolve()
        path = (directory / name).resolve() if kind == "artifact" else directory
        path.relative_to(directory)
        focus = 1
        title = "当前文件改动" if kind == "changes" else path.name
    if request["operation"] == "locate":
        if not path.exists():
            raise FileNotFoundError(path)
        # 只在资源管理器里选中文件。即使读的是脚本，也不会执行它。
        subprocess.Popen(["explorer.exe", str(path) if path.is_dir() else "/select," + str(path)])
        return {"located": str(path)}
    if kind == "changes":
        data = changes(root, directory)[1].decode("utf8", errors="replace")
    elif kind in ("source", "artifact"):
        data = read_text(path)
    else:
        raise ValueError("请选择源码、测试记录或文件改动。")
    lines = data.splitlines()
    page = 80
    start = min(max(1, (len(lines) - 1) // page * page + 1),
                max(1, int(request.get("first") or ((focus - 1) // page * page + 1))))
    return {"title":title, "path":str(path), "first":start, "focus":focus, "total":len(lines),
            "page":page, "lines":lines[start-1:start-1+page]}


def execute(request):
    op = request["operation"]
    task = request.get("task", "")
    if op in ("read", "locate"):
        return document(request)
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
