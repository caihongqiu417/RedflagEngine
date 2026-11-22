"""保留一次开发任务用过的资料、选定源码的修改和本机复测的原始输出。"""
import difflib
import hashlib
import json
from pathlib import Path
import re
import subprocess
import time
from context_index import source_path
from tempfile import TemporaryDirectory
from uuid import uuid4


def folder(project, task):
    if not re.fullmatch(r"[a-f0-9]{12}", task):
        raise ValueError("任务编号可用 tasks 查询。")
    return project / "captures/context" / task


def store(path, data):
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf8")


def load(path):
    return json.loads(path.read_bytes())


def sources(root, names):
    # 本工具只记录开发者选中的源码和测试。相对路径也可指向本次准备新增的文件。
    result = {}
    for name in names:
        rel, path = source_path(root, name)
        result[rel] = path
    return result


def begin(root, project, intent, files):
    if not intent.strip():
        raise ValueError("请填写要完成的修改。")
    selected = sources(root, files)
    task = uuid4().hex[:12]
    destination = folder(project, task)
    destination.parent.mkdir(parents=True, exist_ok=True)
    with TemporaryDirectory(dir=destination.parent) as temp:
        stage = Path(temp) / "task"
        stage.mkdir()
        before = {}
        for name, path in selected.items():
            before[name] = None
            if path.exists():
                data = path.read_bytes()
                before[name] = hashlib.sha256(data).hexdigest()
                saved = stage / "before" / name
                saved.parent.mkdir(parents=True, exist_ok=True)
                saved.write_bytes(data)
        store(stage/"task.json", dict(task=task, intent=intent, before=before, root=str(root.resolve())))
        stage.rename(destination)
    return {"task":task, "intent":intent, "files":list(selected)}


def tasks(project):
    paths = sorted((project/"captures/context").glob("*/task.json"), key=lambda p:p.stat().st_mtime_ns, reverse=True)
    return {"tasks":[{key: row[key] for key in ("task", "intent")} for row in (load(p) for p in paths[:50])],
            "more":max(0, len(paths)-50)}


def event(directory, kind, data, artifacts=None):
    # 每项单独写完再发布。两位作者同时留批注，各自的文件都能保留。
    name = f"{time.time_ns()}-{uuid4().hex[:8]}"
    with TemporaryDirectory(dir=directory) as temp:
        stage = Path(temp)/"event"
        stage.mkdir()
        store(stage/"event.json", dict(kind=kind, **data))
        for file, content in (artifacts or {}).items():
            (stage/file).write_bytes(content)
        stage.rename(directory/name)
    return directory/name


def record_query(root, project, task, operation, query, result):
    directory = folder(project, task)
    if Path(load(directory/"task.json")["root"]) != root.resolve():
        raise ValueError("请使用任务创建时的源码目录。")
    locations = [result] if "source" in result else list(result.get("matches", []))
    for key in ("implementation", "references", "tests", "runners"):
        locations.extend(result.get(key, []))
    paths = {r["source"].rpartition(":")[0] for r in locations}
    originals = directory/"sources"
    originals.mkdir(exist_ok=True)
    copies = {}
    for name, path in sources(root, paths).items():
        if not path.exists():
            continue
        data = path.read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        if not (originals/digest).exists():
            with TemporaryDirectory(dir=originals) as temp:
                draft = Path(temp)/"source"
                draft.write_bytes(data)
                draft.replace(originals/digest)
        copies[name] = digest
    info = event(directory, "reference", {"operation":operation, "query":query, "result":result, "sources":copies})
    return {**result, "record":str(info/"event.json")}


def note(project, task, text):
    if not text.strip():
        raise ValueError("请填写更正意见。")
    path = event(folder(project, task), "note", {"text":text})/"event.json"
    return {"task":task, "text":text, "evidence":str(path)}


def changes(root, directory):
    task = load(directory/"task.json")
    if Path(task["root"]) != root.resolve():
        raise ValueError("请从创建任务时的引擎源码目录复查。")
    rows, patch = {}, []
    for name, path in sources(root, task["before"]).items():
        data = path.read_bytes() if path.is_file() else None
        digest = hashlib.sha256(data).hexdigest() if data is not None else None
        rows[name] = {"before":task["before"][name], "now":digest, "changed":digest != task["before"][name]}
        if not rows[name]["changed"]:
            continue
        before = (directory/"before"/name).read_bytes() if task["before"][name] else b""
        patch.extend(difflib.diff_bytes(difflib.unified_diff, before.splitlines(keepends=True),
                     (data or b"").splitlines(keepends=True), ("a/"+name).encode() if task["before"][name] else b"/dev/null",
                     ("b/"+name).encode() if data is not None else b"/dev/null"))
    return rows, b"".join(patch)


def verify(root, project, task, command):
    if not command:
        raise ValueError("verify 后需要给出实际测试命令。")
    directory = folder(project, task)
    initial, patch = changes(root, directory)
    try:
        run = subprocess.run(command, cwd=root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        code, log = run.returncode, run.stdout
    except OSError as error:
        code, log = 127, str(error).encode()
    final, _ = changes(root, directory)
    record = dict(command=command, exit_code=code, files={p:v["now"] for p,v in initial.items()},
                  changed_during_check=initial != final)
    saved = event(directory,"check",record, {"test.log":log,"changes.diff":patch})
    return {"task":task, **record, "log":str(saved/"test.log"), "diff":str(saved/"changes.diff")}


def review(root, project, task):
    directory = folder(project, task)
    record = load(directory/"task.json")
    files, patch = changes(root, directory)
    result = dict(task=task, intent=record["intent"], files=files, references=[], checks=[], notes=[],
                  evidence=str(directory), diff_preview=patch.decode("utf8", errors="replace").splitlines()[:20])
    for path in sorted(directory.glob("*-*/event.json")):
        item = load(path)
        if item["kind"] == "note":
            result["notes"].append(item["text"])
        elif item["kind"] == "reference":
            answer = item["result"]
            entries = [answer] if "source" in answer else answer.get("matches", [])
            result["references"].append(dict(query=item["query"], operation=item["operation"],
                        sources=[r["source"] for r in entries], evidence=str(path)))
        elif item["kind"] == "check":
            current = not item["changed_during_check"] and all(files[p]["now"] == digest for p,digest in item["files"].items())
            result["checks"].append(dict(command=item["command"], exit_code=item["exit_code"], current=current,
                        log=str(path.with_name("test.log")), diff=str(path.with_name("changes.diff"))))
    result["total"] = {key:len(result[key]) for key in ("references","checks","notes")}
    for key in result["total"]:
        result[key] = result[key][-8:]
    return result
