"""用本机模型分别修两份 Redflag 源码，保留每次请求、模型计数、补丁与原生测试结果。"""

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess as sp
import sys
import tempfile
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REV = "bb1f5e1dba32576ec122308f6473eed902e2a876"
TASKS = {
    "trace": "Fix JobSystem tracing in the existing C++ engine. Calling Trace(true) again during a recording must preserve completed and in-flight timings. Repeating Trace(false) leaves it off. A real off/on transition starts a new recording and late completions from an older session stay out. Preserve existing callers and locking.",
    "pump": "Add void JobSystem::Pump(unsigned maxJobs) in the existing C++ engine; preserve the original void Pump() entry point. The overload runs at most maxJobs main-thread jobs that were already ready when called. Even with a generous budget, newly ready jobs wait for the next Pump. Zero runs none and still enforces main-thread ownership. Reuse the existing execution logic.",
}
SYSTEM = """You maintain an existing C++ engine. Inspect the relevant source before editing. Use only the listed actions. Return a single JSON action, without prose. search/read are ordinary project text tools. If offered, find/show return the project's API index, source locations and usage. Prefer an available precise symbol lookup when it is helpful. read shows 1-based numbered source lines. edit replaces inclusive line ranges; give all lines in the replacement string without line numbers. Unchanged parts of the file are preserved. Multiple edits in one action all use the pre-edit line numbers. An edit triggers an actual C++ test. Correct compilation and behavior determine completion. Example edit format: {"op":"edit","changes":[{"path":"path/from/root.cpp","start":4,"end":5,"text":"new code\nother line"}]}. Paths use /. Limit your search to the task."""
OPS = {
    "search": "search: q is a literal from the task to search for in source.",
    "read": "read: path, start, end identify source lines.",
    "find": "find: q searches the project's API symbols and summaries.",
    "show": "show: q is the fully qualified symbol to inspect.",
    "edit": "edit: changes is a list of path, start, end, text replacements.",
    "test": "test: run C++ behavioral checks.",
}


def run(args, cwd=None, env=None, timeout=60):
    return sp.run(args, check=False, cwd=cwd, env=env, timeout=timeout, capture_output=True)


def checked(args, cwd=None):
    result = run(args, cwd)
    if result.returncode:
        raise RuntimeError(result.stderr.decode("utf8", "replace"))
    return result.stdout


def save(path, value):
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf8")


def fetch(url, payload=None):
    body = None if payload is None else json.dumps(payload, ensure_ascii=False).encode()
    req = urllib.request.Request(url, data=body, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=180) as r:
        return json.load(r)


def clean_copy(folder, index):
    checked(["git", "clone", "--quiet", "--shared", "--no-checkout", str(ROOT), str(folder)])
    checked(
        [
            "git",
            "sparse-checkout",
            "set",
            "--no-cone",
            "/Redflag/src/",
            "/RedflagForge/src/",
            "/Redflag-ScriptCore/src/",
            "/REDFServer/",
            "/SandboxProject/Assets/Scripts/",
            "/tests/",
            "/scripts/",
            "/docs/",
            "/output/api/index.html",
        ],
        folder,
    )
    checked(["git", "checkout", "--quiet", "--detach", REV], folder)
    target = folder / "output/api/index.html"
    shutil.copyfile(index, target)
    # 两组均以同一份目录和相同的索引开始，实验 diff 只记录模型实际写入的 C++。
    checked(["git", "add", "-f", "--", "output/api/index.html"], folder)
    checked(
        [
            "git",
            "-c",
            "user.name=Context bench",
            "-c",
            "user.email=bench@localhost",
            "commit",
            "--allow-empty",
            "-qm",
            "Pin identical API index",
        ],
        folder,
    )
    assert not checked(["git", "status", "--porcelain"], folder).strip()
    return checked(["git", "rev-parse", "HEAD^{tree}"], folder).decode().strip()


def compiler_env():
    locator = Path(os.environ["ProgramFiles(x86)"]) / "Microsoft Visual Studio/Installer/vswhere.exe"
    vs = (
        checked(
            [
                str(locator),
                "-latest",
                "-products",
                "*",
                "-requires",
                "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                "-property",
                "installationPath",
            ]
        )
        .decode()
        .strip()
    )
    script = Path(vs) / "VC/Auxiliary/Build/vcvars64.bat"
    with tempfile.TemporaryDirectory(prefix="redflag-compiler-") as directory:
        loader = Path(directory) / "compile-env.cmd"
        loader.write_text(
            f'@echo off\ncall "{script}" >nul\nif errorlevel 1 exit /b 1\nset\n', encoding="mbcs"
        )
        data = checked(["cmd", "/d", "/c", str(loader)]).decode("mbcs", "replace")
    return dict(line.split("=", 1) for line in data.splitlines() if "=" in line and not line.startswith("="))


def compiler_text(raw):
    for encoding in ("utf8", "gb18030", "mbcs"):
        try:
            return raw.decode(encoding)
        except UnicodeDecodeError:
            pass
    return raw.decode("utf8", "replace")


def verify(task, copy, build, env, raw_log):
    cpp = copy / "Redflag/src/Redflag/Core/JobSystem.cpp"
    compiler = shutil.which("cl.exe", path=next(v for k, v in env.items() if k.upper() == "PATH"))
    cmd = [
        compiler,
        "/nologo",
        "/std:c++17",
        "/EHsc",
        "/MD",
        "/utf-8",
        "/DNOMINMAX",
        "/DRF_PLATFORM_WINDOWS",
        "/wd4005",
        "/wd4828",
        "/I" + str(copy / "Redflag/src"),
        "/I" + str(copy / "Redflag/src/Redflag"),
        "/I" + str(build / "support"),
        "/I" + str(ROOT / "Redflag/vendor/spdlog/include"),
        "/I" + str(ROOT / "Redflag/vendor/fmt/include"),
    ]
    if task == "pump":
        cmd.append("/DTEST_PUMP")
    result = run(
        cmd + [str(ROOT / "tests/fixtures/JobContextTasks.cpp"), str(cpp), "/Fe:verify.exe"], build, env
    )
    raw_log.write_bytes(result.stdout + result.stderr)
    compile_log = compiler_text(raw_log.read_bytes())
    if result.returncode:
        return False, compile_log
    try:
        result = run([str(build / "verify.exe")], build, timeout=8)
    except sp.TimeoutExpired:
        return False, compile_log + "\nTest process timed out"
    return result.returncode == 0, compile_log + "\n" + (result.stdout + result.stderr).decode(
        "utf8", "replace"
    )


def source_path(copy, request):
    path = (copy / request.replace("\\", "/").lstrip("/")).resolve()
    if (
        not path.is_relative_to(copy)
        or path.suffix not in (".h", ".cpp", ".hpp", ".inl", ".cs")
        or not path.is_file()
    ):
        raise ValueError("choose an existing project-relative source file")
    return path


def text_action(op, a, copy, cli):
    if op in ("find", "show"):
        cmd = [sys.executable, str(cli), "--root", str(copy), op, a["q"]]
        if op == "find":
            cmd += ["--limit", "4"]
        result = run(cmd, copy)
        return (result.stdout + result.stderr).decode("utf8", "replace")
    if op == "search":
        query = a["q"]
        cmd = [
            "rg",
            "-n",
            "--no-heading",
            "--color",
            "never",
            "--sort",
            "path",
            "--max-count",
            "8",
            "-F",
            "-g",
            "*.h",
            "-g",
            "*.cpp",
            "-g",
            "*.cs",
            "--",
            query,
            "Redflag/src",
            "RedflagForge/src",
            "tests",
        ]
        result = run(cmd, copy)
        return (
            "\n".join(result.stdout.decode("utf8", "replace").splitlines()[:20])
            or "(no matching source line)"
        )
    if op == "read":
        path = source_path(copy, a["path"])
        lines = path.read_text(encoding="utf8", errors="replace").splitlines()
        first = max(1, int(a.get("start", 1)))
        end = min(len(lines), int(a.get("end", first + 59)), first + 89)
        return "\n".join(f"{i}: {lines[i - 1]}" for i in range(first, end + 1)) or "(range outside file)"
    raise ValueError("unknown action")


def edit(changes, copy):
    files = {}
    for c in changes:
        p = source_path(copy, c["path"])
        if not p.is_relative_to(copy / "Redflag/src") or p.suffix not in (".h", ".cpp"):
            raise ValueError("edit production C++ only")
        files.setdefault(p, []).append(c)
    if not files:
        raise ValueError("provide at least one source change")
    prepared = {}
    for p, ops in files.items():
        lines = p.read_text(encoding="utf8").splitlines()
        limit = len(lines) + 1
        for c in sorted(ops, key=lambda e: -e["start"]):
            s, e = c["start"], c["end"]
            if not 1 <= s <= e < limit:
                raise ValueError("overlapping or invalid 1-based line range")
            if not isinstance(c["text"], str):
                raise TypeError("replacement text must be a string")
            lines[s - 1 : e] = c["text"].splitlines()
            limit = s
        prepared[p] = "\n".join(lines) + "\n"
    for p, s in prepared.items():
        p.write_text(s, encoding="utf8")


def method_context(task, mode, copy, cli):
    method = {"trace": "Trace", "pump": "Pump"}[task]
    if mode == "context":
        raw = text_action("show", {"q": "Redflag::JobSystem::" + method}, copy, cli)
        index = json.loads(raw)
        header = index["source"]
        code = index["implementation"][0]["source"]
    else:
        pattern = rf"(?:\bvoid\s+|\.|->|JobSystem::){method}\s*\("
        args = [
            "rg",
            "--sort",
            "path",
            "-n",
            "--no-heading",
            "--color",
            "never",
            "-g",
            "*.h",
            "-g",
            "*.cpp",
            pattern,
            "Redflag/src",
            "RedflagForge/src",
            "tests",
        ]
        raw = checked(args, copy).decode("utf8", "replace").replace("\\", "/")
        matches = [(p, n, t) for p, n, t in (line.split(":", 2) for line in raw.splitlines())]
        p, n, _ = next(m for m in matches if m[0].endswith("/JobSystem.h") and "void " in m[2])
        header = f"{p}:{n}"
        p, n, _ = next(m for m in matches if "JobSystem::" + method + "(" in m[2])
        code = f"{p}:{n}"
    hp, hn = header.rsplit(":", 1)
    cp, cn = code.rsplit(":", 1)
    hn, cn = int(hn), int(cn)
    lines = source_path(copy, cp).read_text(encoding="utf8").splitlines()
    depth = 0
    for end, line in enumerate(lines[cn - 1 :], cn):
        depth += line.count("{") - line.count("}")
        if "}" in line and depth == 0:
            break
    head = text_action("read", {"path": hp, "start": hn - 2, "end": hn + 2}, copy, cli)
    implementation = text_action("read", {"path": cp, "start": cn, "end": end}, copy, cli)
    return raw, head, implementation, (hp, hn, cp, cn, end)


def attempt(args, task, mode, iteration, env):
    label = (
        f"{task}-{mode}-{iteration}" + ("-selected" if args.selected else "") + ("-cpp" if args.cpp else "")
    )
    out = args.output / label
    out.mkdir(parents=True, exist_ok=False)
    copy = Path(tempfile.mkdtemp(prefix=label + "-", dir=args.work)) / "project"
    tree = clean_copy(copy, args.index)
    build = copy.parent / "compile"
    build.mkdir()
    dep = Path("Redflag/Debug/Instrumentor.h")
    (build / "support" / dep.parent).mkdir(parents=True)
    shutil.copyfile(ROOT / "Redflag/src" / dep, build / "support" / dep)
    initial, log = verify(task, copy, build, env, out / "before-compiler.bin")
    if initial:
        raise RuntimeError("benchmark task already passes before the AI edit")
    (out / "before-test.txt").write_text(log, encoding="utf8")
    if task == "trace" and "lost a completed" not in log:
        raise RuntimeError("baseline did not reach the intended behavior check: " + log)
    if task == "pump" and "C2660" not in log:
        raise RuntimeError("missing Pump overload was not observed: " + log)
    tools = ["search", "read", "edit", "test"]
    if mode == "context":
        tools += ["find", "show"]
    shared = [{"role": "system", "content": SYSTEM}]
    session = [
        {
            "role": "user",
            "content": "Available actions for this session:\n"
            + "\n".join(OPS[k] for k in tools)
            + "\n\nActual task:\n"
            + TASKS[task],
        }
    ]
    if args.selected:
        raw, header, impl, span = method_context(task, mode, copy, args.cli)
        (out / "lookup.txt").write_text(raw, encoding="utf8")
        shared = [
            {
                "role": "system",
                "content": "You edit an existing C++ method selected in an IDE. Return only JSON with strings implementation and declaration. implementation replaces the entire original method, including the full signature, braces, and any overload. declaration replaces the selected declaration line. Use an empty declaration string if unchanged. There are no enclosing namespace braces in the replacement. Do not include line numbers. The C++ compiler and behavioral tests run automatically; retry only when they fail.",
            }
        ]
        session = [
            {
                "role": "user",
                "content": TASKS[task]
                + "\n\nProject lookup results:\n"
                + raw
                + "\nDeclaration with surrounding lines:\n"
                + header
                + "\nSelected method definition:\n"
                + impl
                + "\nProvide the complete replacement using implementation and declaration.",
            }
        ]
        if args.cpp:
            shared = [
                {
                    "role": "system",
                    "content": "You edit C++ code. Output the corrected complete .cpp function definitions in one <cpp>...</cpp> section. Put the public .h declarations in one <header>...</header> section if the signature changes. Both replace the original selected functions, within their existing namespace and class. Keep existing types. Do not include line numbers, markdown, or prose.",
                }
            ]
            session[0]["content"] = session[0]["content"].replace(
                "Provide the complete replacement using implementation and declaration.",
                "Write the corrected <cpp> definitions now and add <header> declarations only if needed.",
            )
        original_header = (copy / span[0]).read_bytes()
        original_code = (copy / span[2]).read_bytes()
    history = []
    rounds = []
    success = False
    for turn in range(1, args.turns + 1):
        # 两组共享相同的最近工具结果窗口；完整原文始终保存在各轮 JSON 里。
        recent = []
        for pair in reversed(history):
            trial = pair + recent
            if sum(len(m["content"]) for m in trial) > 7200 and recent:
                break
            recent = trial
        messages = shared + session + recent
        payload = {
            "model": args.model,
            "messages": messages,
            "temperature": 0,
            "seed": 41 + iteration,
            "max_tokens": 550,
            "response_format": {"type": "json_object"},
            "stream": False,
        }
        if args.cpp:
            payload.pop("response_format")
        save(out / f"{turn:02d}-request.json", payload)
        try:
            response = fetch(args.endpoint + "/chat/completions", payload)
        except urllib.error.HTTPError as e:
            response = {"http_error": e.code, "body": e.read().decode("utf8", "replace")}
        except (urllib.error.URLError, TimeoutError) as e:
            response = {"transport_error": str(e)}
        save(out / f"{turn:02d}-response.json", response)
        if "usage" not in response:
            rounds.append({"turn": turn, "error": response, "op": None, "usage": None})
            break
        content = response["choices"][0]["message"]["content"]
        op = None
        try:
            unwrapped = re.sub(r"^\s*```(?:json)?\s*|\s*```\s*$", "", content.strip(), flags=re.IGNORECASE)
            if args.cpp:
                source = re.search(r"<cpp>(.*?)</cpp>", content, re.DOTALL | re.IGNORECASE)
                declaration = re.search(r"<header>(.*?)</header>", content, re.DOTALL | re.IGNORECASE)
                if not source:
                    raise ValueError("provide <cpp>complete source</cpp>")
                a = {
                    "implementation": source[1].strip(),
                    "declaration": declaration[1].strip() if declaration else "",
                }
            else:
                a = json.loads(unwrapped, strict=not args.selected)
            if args.selected:
                hp, hn, cp, cn, end = span
                (copy / hp).write_bytes(original_header)
                (copy / cp).write_bytes(original_code)
                changes = [{"path": cp, "start": cn, "end": end, "text": a["implementation"]}]
                if a.get("declaration"):
                    changes.append({"path": hp, "start": hn, "end": hn, "text": a["declaration"]})
                a = {"op": "edit", "changes": changes}
            op = a["op"]
            if op not in tools:
                raise ValueError("action not available in this session")
            if op in ("edit", "test"):
                if op == "edit":
                    edit(a["changes"], copy)
                success, result = verify(task, copy, build, env, out / f"{turn:02d}-compiler.bin")
                (out / f"{turn:02d}-test.txt").write_text(result, encoding="utf8")
                result = ("PASS " if success else "FAIL ") + result.replace(str(copy), "PROJECT").replace(
                    str(ROOT), "ENGINE"
                )
            else:
                result = text_action(op, a, copy, args.cli)
        except (ValueError, KeyError, TypeError, OSError, IndexError) as error:
            result = "Invalid action: " + str(error)
        (out / f"{turn:02d}-tool.txt").write_text(result, encoding="utf8")
        reply = result[:4400]
        if len(reply) < len(result):
            reply += "\n[output abbreviated; use a narrower symbol or read range]"
        update = (
            "Supply a corrected full implementation and declaration against the original selected text."
            if args.selected
            else "Inspect the method definition (not just the header) and then edit. Next JSON action:"
        )
        history.append(
            [
                {"role": "assistant", "content": content},
                {
                    "role": "user",
                    "content": "Tool result:\n"
                    + reply
                    + "\nContinue your original task: "
                    + TASKS[task]
                    + "\n"
                    + update,
                },
            ]
        )
        rounds.append({"turn": turn, "op": op, "usage": response["usage"], "test_passed": success})
        if success:
            break
    diff = checked(
        ["git", "-c", "core.autocrlf=false", "diff", "--ignore-space-at-eol", "--", "Redflag/src"], copy
    )
    (out / "model.patch").write_bytes(diff)
    summary = {
        "task": task,
        "mode": mode,
        "iteration": iteration,
        "selected": args.selected,
        "cpp": args.cpp,
        "seed": 41 + iteration,
        "base": REV,
        "tree": tree,
        "model": args.model,
        "index_sha256": hashlib.sha256(args.index.read_bytes()).hexdigest(),
        "cli_sha256": hashlib.sha256(args.cli.read_bytes()).hexdigest(),
        "shared_header_sha256": hashlib.sha256((build / "support" / dep).read_bytes()).hexdigest(),
        "ok": success,
        "rounds": rounds,
        "input_tokens": sum(r["usage"]["prompt_tokens"] for r in rounds if r["usage"]),
        "output_tokens": sum(r["usage"]["completion_tokens"] for r in rounds if r["usage"]),
    }
    save(out / "summary.json", summary)
    print(label, success, summary["input_tokens"], summary["output_tokens"], flush=True)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--endpoint", default="http://127.0.0.1:18157/v1")
    p.add_argument("--model", default="Qwen3-VL-2B-Instruct")
    p.add_argument("--cli", type=Path, default=ROOT / "scripts/dev_context.py")
    p.add_argument("--index", type=Path, default=ROOT / "output/api/index.html")
    p.add_argument("--output", type=Path, default=ROOT / "output/validation/dev-context")
    p.add_argument("--work", type=Path, default=ROOT / "tmp/dev-context-bench")
    p.add_argument("--mode", choices=("context", "classic"), required=True)
    p.add_argument("--task", choices=TASKS, required=True)
    p.add_argument("--iteration", type=int, default=1)
    p.add_argument("--turns", type=int, default=10)
    p.add_argument("--selected", action="store_true", help="对照 IDE 选定同一符号后实际修改、测试的成本")
    p.add_argument("--cpp", action="store_true", help="选中符号后返回 C++ 源码块")
    args = p.parse_args()
    args.output = args.output.resolve()
    args.work = args.work.resolve()
    args.cli = args.cli.resolve(strict=True)
    args.index = args.index.resolve(strict=True)
    if args.cpp and not args.selected:
        p.error("--cpp requires --selected")
    args.work.mkdir(parents=True, exist_ok=True)
    args.output.mkdir(parents=True, exist_ok=True)
    props = fetch(args.endpoint[:-3] + "/props")
    save(
        args.output / "model-config.json",
        {
            "model": fetch(args.endpoint + "/models")["data"][0],
            "build": props.get("build_info"),
            "n_ctx": props["default_generation_settings"]["n_ctx"],
            "index_sha256": hashlib.sha256(args.index.read_bytes()).hexdigest(),
        },
    )
    attempt(args, args.task, args.mode, args.iteration, compiler_env())


if __name__ == "__main__":
    main()
