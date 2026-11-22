"""在当前 Redflag 源码中查接口、追调用，记录一项经过验证的改动。"""
import argparse
import json
import os
from pathlib import Path
import sys

import context_index
import context_review


def lookup(root, project, operation, query, limit=4, task=""):
    if operation not in ("find", "show"):
        raise ValueError("请选择 find 或 show。")
    result = (context_index.find if operation == "find" else context_index.show)(root, query, limit)
    return context_review.record_query(root, project, task, operation, query, result) if task else result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--project", type=Path, default=os.environ.get("REDFLAG_PROJECT"))
    actions = parser.add_subparsers(dest="op", required=True)
    for name in ("find", "show"):
        sub = actions.add_parser(name)
        sub.add_argument("query")
        sub.add_argument("--limit", type=int, default=4)
        sub.add_argument("--task", default="")
    start = actions.add_parser("start")
    start.add_argument("intent")
    start.add_argument("--files", nargs="+", default=[])
    actions.add_parser("tasks")
    for name in ("note", "review", "verify"):
        sub = actions.add_parser(name)
        sub.add_argument("task")
        if name == "note":
            sub.add_argument("text")
        if name == "verify":
            sub.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    root = args.root.resolve()
    project = (args.project or root/"RedflagForge").resolve()
    if args.op in ("find", "show"):
        result = lookup(root, project, args.op, args.query, args.limit, args.task)
    elif args.op == "tasks":
        result = context_review.tasks(project)
    elif args.op == "start":
        result = context_review.begin(root, project, args.intent, args.files)
    elif args.op == "note":
        result = context_review.note(project, args.task, args.text)
    elif args.op == "review":
        result = context_review.review(root, project, args.task)
    else:
        command = args.command[1:] if args.command[:1] == ["--"] else args.command
        result = context_review.verify(root, project, args.task, command)
    print(json.dumps(result, ensure_ascii=False))
    return result.get("exit_code", 0)


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf8")
    try:
        sys.exit(main())
    except (ValueError, OSError, KeyError) as error:
        print(json.dumps({"error": str(error)}, ensure_ascii=False))
        sys.exit(1)
