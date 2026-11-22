"""共用 API 手册的声明。实现、可能的调用点和测试入口从当前工作目录查找。"""
from functools import lru_cache
import json
from pathlib import Path
import re

CODE_DIRS = ("Redflag/src", "RedflagForge/src", "Redflag-ScriptCore/Source", "SandboxProject/Source", "tests")
SOURCE_TYPES = {".cpp", ".c", ".cc", ".h", ".hpp", ".cs", ".py", ".cmd", ".ps1", ".sh",
                ".glsl", ".vert", ".frag", ".comp", ".md", ".tex", ".html", ".json", ".txt", ".yaml", ".yml"}


def source_path(root, name):
    path = (root / name).resolve()
    rel = path.relative_to(root.resolve()).as_posix()
    if path.suffix.lower() not in SOURCE_TYPES or not rel.startswith(tuple(d + "/" for d in (*CODE_DIRS, "scripts", "docs"))):
        raise ValueError(f"请选择仓库中的源码、文档或测试文件：{name}")
    return rel, path


def read_text(path):
    data = path.read_bytes()
    for encoding in ("utf-8-sig", "gb18030"):
        try:
            return data.decode(encoding)
        except UnicodeDecodeError:
            pass
    return data.decode("utf8", errors="replace")


@lru_cache(maxsize=2)
def _load(path, modified, size):
    html = path.read_text(encoding="utf8")
    start = html.index('<script id="db" type="application/json">') + len('<script id="db" type="application/json">')
    return json.loads(html[start:html.index("</script>", start)])


def database(root):
    index = root / "output/api/index.html"
    stat = index.stat()
    return _load(index, stat.st_mtime_ns, stat.st_size)


def symbol(row):
    group, owner, name = row[:3]
    return owner + ("." if group == "C# 脚本" else "::") + name if owner else name


def entry(row):
    return dict(symbol=symbol(row), source=f"{row[5]}:{row[6]}", signature=row[4], summary=row[7])


def find(root, query, limit=4):
    query = query.strip().casefold()
    if not query or not 1 <= limit <= 20:
        raise ValueError("请输入关键词，条数可选 1 至 20。")
    words = re.findall(r"[a-z0-9_]+|[\u3400-\u9fff]+", query)
    terms = set(words)
    for word in words:
        if len(word) > 2 and "\u3400" <= word[0] <= "\u9fff":
            terms.update(word[i:i+2] for i in range(len(word)-1))
    ranked = []
    for row in database(root)["entries"]:
        full, name = symbol(row).casefold(), row[2].casefold()
        fields = (name, full, row[5].casefold(), row[7].casefold(), row[0].casefold())
        score = sum(max((weight for field, weight in zip(fields, (9, 5, 3, 4, 2)) if term in field), default=0)
                    for term in terms)
        if score:
            exact = full == query or name == query or full.endswith("::" + query) or full.endswith("." + query)
            ranked.append((exact, score, row[3] in ("类", "结构体", "枚举"), row))
    ranked.sort(key=lambda hit: hit[:3], reverse=True)
    matches = [entry(item[3]) for item in ranked[:limit]]
    if not matches:
        # 项目脚本和非公开实现没有 API 声明。文件名同样可以作为下一次 show 的入口。
        for directory in (*CODE_DIRS, "scripts"):
            for file in sorted((root/directory).rglob("*")):
                if file.suffix.lower() in SOURCE_TYPES and any(t in file.stem.casefold() for t in terms):
                    matches.append(dict(symbol=file.stem, source=f"{file.relative_to(root).as_posix()}:1",
                                        signature="", summary="源文件"))
        return dict(query=query, matches=matches[:limit], more=max(0,len(matches)-limit))
    return {"query": query, "matches": matches, "more": max(0, len(ranked)-limit)}


def lookup_sites(root, row, db):
    name, method = row[2], row[3] == "方法"
    owner = row[1].split("::")[-1]
    # 这里只识别字面出现的类型和调用形式。多态调用和同名的其他类型仍要读该行确认。
    token = re.compile(r"(?<!\w)" + re.escape(name) + (r"\s*\(" if method else r"(?!\w)"))
    owners = {owner}
    if method:
        for item in db["entries"]:
            if item[3] in ("类", "结构体") and re.search(r"\b(?:public|protected)\s+" + re.escape(owner) + r"\b", item[4]):
                owners.add(item[2])
    qualified = re.compile(r"(?<!\w)(?:" + "|".join(map(re.escape, sorted(owners))) + r")::" + re.escape(name) + r"\s*\(")
    results = dict(implementation=[], references=[], tests=[], runners=[])
    test_files = set()
    for directory in CODE_DIRS:
        for path in sorted((root / directory).rglob("*")):
            if path.suffix not in (".cpp", ".cc", ".h", ".hpp", ".cs", ".py") or path.is_symlink():
                continue
            relative = path.relative_to(root).as_posix()
            text = read_text(path)
            if name not in text:
                continue
            for line_number, line in enumerate(text.splitlines(), 1):
                clean = line.strip()
                code = re.sub(r'''"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|//.*''', "", clean)
                if not token.search(code) or clean.startswith(("//", "/*", "*", "#")):
                    continue
                if relative == row[5] and line_number == row[6]:
                    continue
                if directory == "tests":
                    test_files.add(path)
                    group = "tests"
                elif method and path.suffix == ".cpp" and qualified.search(code) and not code.endswith(";"):
                    group = "implementation"
                elif relative == row[5] or path.suffix in (".h", ".hpp"):
                    continue
                else:
                    group = "references"
                results[group].append(dict(source=f"{relative}:{line_number}", text=clean[:230]))

    compiled = Path(row[5]).with_suffix(".cpp").as_posix() if row[5].endswith((".h",".hpp")) else ""
    for runner in sorted((root / "tests").glob("*")):
        if runner.suffix not in (".cmd", ".ps1", ".py", ".sh"):
            continue
        for number, line in enumerate(read_text(runner).splitlines(), 1):
            normalized = line.replace("\\", "/")
            if (compiled and compiled in normalized) or any(path.name in line and path != runner for path in test_files):
                results["runners"].append(dict(source=f"tests/{runner.name}:{number}", text=line.strip()[:230]))
                break
    # 每类先给不同文件各一处，使一个大型测试文件不会挤走其他入口。
    for group, rows in results.items():
        first, other = {}, []
        for item in rows:
            file = item["source"].rpartition(":")[0]
            if file in first:
                other.append(item)
            else:
                first[file] = item
        results[group] = list(first.values()) + other
    return results


def show(root, query, limit=4):
    if not 1 <= limit <= 20 or not query.strip():
        raise ValueError("请输入符号或文件行号，条数可选 1 至 20。")
    db = database(root)
    wanted = query.strip().casefold()
    rows = [r for r in db["entries"] if f"{r[5]}:{r[6]}".casefold() == wanted or
            r[2].casefold() == wanted or symbol(r).casefold() == wanted or
            symbol(r).casefold().endswith(("::" + wanted, "." + wanted))]
    if len(rows) != 1:
        if not rows:
            location, colon, number = query.strip().rpartition(":")
            if colon and number.isdecimal():
                relative, path = source_path(root, location)
                lines = read_text(path).splitlines()
                line = int(number)
                if not 1 <= line <= len(lines):
                    raise ValueError(f"行号应在 1 至 {len(lines)} 之间。")
                return dict(source=f"{relative}:{line}", symbol=path.name, lines=[
                    {"line":n, "text":lines[n-1][:350]} for n in range(max(1,line-2), min(len(lines),line+2*limit)+1)])
            return find(root, query, limit)
        return dict(query=query, matches=[entry(r) for r in rows[:limit]], more=max(0,len(rows)-limit))
    r = rows[0]
    answer = entry(r)
    path = root / r[5]
    indexed = db["sources"][r[5]]
    if not path.is_file() or read_text(path).splitlines() != indexed:
        answer.update(index_current=False, message="声明文件在生成手册后已有变化，请查原文件并运行 docs/api/generate.py 再使用签名。")
        return answer
    if not answer["summary"]:
        # 早期 2D 接口多用普通行注释，手册中同一行之前的说明也能直接读到。
        start = r[6]-1
        if start and indexed[start-1].strip().startswith("//"):
            answer["summary"] = indexed[start-1].strip().lstrip("/").strip()
        elif start and indexed[start-1].strip().startswith("/*") and indexed[start-1].strip().endswith("*/"):
            answer["summary"] = indexed[start-1].strip()[2:-2].strip()
    evidence = lookup_sites(root, r, db)
    # 虚函数声明可能只有后端实现；同名调用只作为查源码的线索。
    answer.update(index_current=True, **{key: items[:limit] for key, items in evidence.items()})
    answer["more"] = {key: len(items)-limit for key, items in evidence.items() if len(items)>limit}
    return answer
