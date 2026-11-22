"""把一次创作改动的意图、复测步骤和实际文件整理成 Forge 可以打开的记录。"""
import argparse
import hashlib
import json
import os
import subprocess
from pathlib import Path
from tempfile import NamedTemporaryFile
from PIL import Image

from project import ENGINE, PROJECT


MANIFEST = "creation.review.json"
IMAGE = {".jpg", ".jpeg", ".png"}
TEXT = {".json", ".txt", ".csv", ".tsv", ".md"}


def validation(project):
    if project.resolve() == (ENGINE / "RedflagForge").resolve():
        return ENGINE / "output/validation"
    return project.resolve() / "output/validation"


def directory(project, case):
    root = validation(project)
    if not case or case in {".", ".."} or Path(case).name != case or (root / case).resolve().parent != root.resolve():
        raise ValueError("用验证目录内的一个文件夹名作为案例名称。")
    return root / case


def words(record, key):
    text = record.get(key) if isinstance(record, dict) else None
    if not isinstance(text, str) or not text.strip():
        raise ValueError(f"请填写 {key}。")
    return text.strip()


def inspect_review(folder, record, seal):
    if not isinstance(record, dict) or record.get("version", 1) != 1:
        raise ValueError("无法读取这份审查记录的版本。")
    result = dict(version=1, title=words(record, "title"), intent=words(record, "intent"))
    for group, fields in (("systems", ("name","change")), ("steps", ("action","expect")), ("evidence", ("label","file"))):
        rows = record.get(group)
        if not isinstance(rows, list) or not rows:
            raise ValueError(f"{group} 至少需要一项。")
        result[group] = []
        for item in rows:
            row = {key:words(item,key) for key in fields}
            if group == "evidence":
                relative = Path(row["file"])
                path = (folder / relative).resolve()
                if relative.is_absolute() or not path.is_relative_to(folder.resolve()) or path.suffix.lower() not in IMAGE | TEXT:
                    raise ValueError(f"证据应为案例目录中的图片或文字文件：{relative}")
                if path == (folder / MANIFEST).resolve():
                    raise ValueError("审查记录不能引用它自己。")
                digest = hashlib.sha256()
                with path.open("rb") as stream:
                    for block in iter(lambda: stream.read(256*1024), b""):
                        digest.update(block)
                    if seal:
                        stream.seek(0)
                        if path.suffix.lower() in IMAGE:
                            with Image.open(stream) as picture:
                                picture.verify()
                        else:
                            stream.read().decode("utf-8-sig")
                row["file"] = relative.as_posix()
                row["sha256"] = digest.hexdigest()
                if not seal and row["sha256"] != item.get("sha256"):
                    raise ValueError(f"证据已变动：{relative}")
            result[group].append(row)
    return result


def publish(project, case, record):
    folder = directory(project, case)
    checked = inspect_review(folder, record, True)
    # 所有证据都已读到后才换文件，Forge 重新读取时只会看到一份完整记录。
    with NamedTemporaryFile(mode="w", encoding="utf8", dir=folder, delete=False) as stream:
        temporary = Path(stream.name)
        json.dump(checked, stream, indent=2, ensure_ascii=False)
        stream.write("\n")
    try:
        os.replace(temporary, folder / MANIFEST)
    finally:
        temporary.unlink(missing_ok=True)
    return {"case":case, "file":MANIFEST, "title":checked["title"], "evidence":len(checked["evidence"])}


def read(project, case):
    folder = directory(project, case)
    record = json.loads((folder / MANIFEST).read_text(encoding="utf-8-sig"))
    return inspect_review(folder, record, False)


def cases(project):
    return sorted(path.parent.name for path in validation(project).glob("*/" + MANIFEST) if path.is_file())


def open_review(project, name, page, scene=None, executable=None, frame=None, baseline=None, base_frame=None):
    project = project.resolve()
    if page == "playtest":
        from playtest import run_folder, read_json
        folder = run_folder(project, name)
        scene = scene or read_json(folder / "session.json")["scene"]
        if baseline:
            run_folder(project, baseline)
    elif page == "change":
        from asset_changes import change_folder
        change_folder(project, name)
    else:
        read(project, name)
    exe = Path(executable or ENGINE / "bin/Debug-windows-x86_64/RedflagForge/RedflagForge.exe").resolve(strict=True)
    command = [str(exe), "--vulkan"]
    if scene:
        command.extend(["--scene", str((project / scene).resolve(strict=True))])
    env = dict(os.environ, REDFLAG_PROJECT=str(project), REDFLAG_REVIEW=name, REDFLAG_REVIEW_PAGE=page)
    for key,value in (("FRAME",frame),("BASELINE",baseline),("BASE_FRAME",base_frame)):
        env["REDFLAG_REVIEW_"+key] = str(value) if value is not None else ""
    child = subprocess.Popen(command,cwd=project,env=env,creationflags=getattr(subprocess,"CREATE_NO_WINDOW",0))
    return {"pid":child.pid,"case":name,"page":page}


def main():
    app = argparse.ArgumentParser(description=__doc__)
    app.add_argument("--project", type=Path, default=PROJECT, help="包含 assets 的游戏项目目录")
    sub = app.add_subparsers(dest="operation", required=True)
    sub.add_parser("list")
    check = sub.add_parser("check")
    check.add_argument("case", nargs="?")
    write = sub.add_parser("publish")
    write.add_argument("case")
    write.add_argument("description", type=Path, help="描述意图、各系统改动、复测步骤和证据相对路径的 JSON")
    show = sub.add_parser("open", help="在 Forge 中直接打开这份记录")
    show.add_argument("case")
    show.add_argument("--page", choices=("steps","images","receipts","playtest","change"), default="steps")
    show.add_argument("--scene", help="可选，打开审查时载入的项目场景")
    show.add_argument("--frame", type=int, help="试玩中要查看的帧号")
    show.add_argument("--baseline", help="并排比较的另一局试玩编号")
    show.add_argument("--base-frame", type=int, help="对照局的帧号")
    args = app.parse_args()
    try:
        if args.operation == "publish":
            result = publish(args.project, args.case, json.loads(args.description.read_text(encoding="utf-8-sig")))
        elif args.operation == "open":
            result = open_review(args.project,args.case,args.page,args.scene,frame=args.frame,baseline=args.baseline,base_frame=args.base_frame)
        elif args.operation == "check":
            result = {name:read(args.project,name)["title"] for name in ([args.case] if args.case else cases(args.project))}
        else:
            result = cases(args.project)
        print(json.dumps(result, ensure_ascii=False, indent=2))
    except (ValueError, OSError) as error:
        app.exit(1, str(error)+"\n")


if __name__ == "__main__":
    main()
