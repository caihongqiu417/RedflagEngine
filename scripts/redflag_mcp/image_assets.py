"""导入美术原图，或从配置的 Images API 生成图。两种入口写同一种 Redflag 贴图资产。"""
import argparse
import base64
from contextlib import contextmanager
from datetime import datetime, timezone
import hashlib
from io import BytesIO
import json
import os
from pathlib import Path
import shutil
import subprocess
from tempfile import gettempdir, TemporaryDirectory
from urllib.parse import urlsplit
from urllib.request import Request, urlopen

from PIL import Image
from ruamel.yaml import YAML

from project import ENGINE, PROJECT


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def png_size(data):
    with Image.open(BytesIO(data), formats=["PNG"]) as image:
        image.verify()
    with Image.open(BytesIO(data), formats=["PNG"]) as image:
        image.load()
        return list(image.size)


def destination(project, collection, name):
    if not name or Path(name).name != name or Path(name).suffix.lower() != ".png":
        raise ValueError("图片名须为一个 PNG 文件名，例如 Ticket.png。")
    root = project.resolve() / "assets"
    folder = Path(collection)
    if not root.is_dir() or root.resolve() != root or not folder.parts or folder.is_absolute() or folder.drive or ".." in folder.parts:
        raise ValueError("请指定包含 assets 的项目和 assets/ui 下的图集目录。")
    path = root
    for part in ("ui", *folder.parts, name):
        path = path / part
        if path.is_symlink() or (path.is_dir() and path.resolve() != path):
            raise ValueError("图片必须保存在这个项目的 assets/ui 内。")
    return path


@contextmanager
def asset_lock(path):
    # CLI 和不同 MCP 进程可能同时更新同一个图标。操作系统在进程退出时释放文件锁。
    key = sha256(os.path.normcase(str(path)).encode("utf8"))
    with (Path(gettempdir()) / ("redflag-image-" + key + ".lock")).open("a+b") as lock:
        lock.seek(0)
        if os.name == "nt":
            import msvcrt
            msvcrt.locking(lock.fileno(), msvcrt.LK_LOCK, 1)
        else:
            import fcntl
            fcntl.flock(lock, fcntl.LOCK_EX)
        yield


def save(project, collection, name, data, source):
    project = Path(project).resolve()
    target = destination(project, collection, name)
    size = png_size(data)
    target.parent.mkdir(parents=True, exist_ok=True)
    source = {**source, "created_utc": datetime.now(timezone.utc).isoformat(), "width": size[0], "height": size[1],
              "asset_sha256": sha256(data)}
    info = Path(str(target) + ".rfsource.yaml")
    meta = Path(str(target) + ".rfmeta")
    index = ENGINE / "bin-int/Tools/Assets/AssetIndex.exe"
    if not index.is_file():
        raise FileNotFoundError("请先在引擎仓库根目录运行 scripts/build_asset_index.cmd。")

    with TemporaryDirectory(dir=project) as scratch, asset_lock(target):
        work = Path(scratch)
        staged = work / name
        staged.write_bytes(data)
        staged_meta = Path(str(staged) + ".rfmeta")
        if info.is_symlink() or meta.is_symlink():
            raise ValueError("资产的来源文件与编号必须位于此项目。")
        # 新编号始终由 C++ AssetLibrary 分配。只有原图丢失时才把尚在的旧编号带到暂存目录。
        register = target if target.is_file() and meta.is_file() else staged
        if register == staged and meta.is_file():
            shutil.copyfile(meta, staged_meta)
        result = subprocess.run([index, project/"assets", "--texture", register],
                                encoding="utf8", capture_output=True, check=False)
        if result.returncode:
            raise ValueError(result.stderr.strip() or "无法登记贴图编号。")
        asset_id = str(int(result.stdout.strip()))
        source["asset_id"] = asset_id
        with (work/"source.yaml").open("w", encoding="utf8") as stream:
            YAML().dump(source, stream)

        # PNG 最后替换。若目标正在被查看器占用，恢复旧来源与编号，原图仍在原处。
        previous = info.exists()
        if previous:
            shutil.copyfile(info, work/"old-source")
        new_meta = not meta.exists()
        os.replace(work/"source.yaml", info)
        try:
            if new_meta:
                os.replace(staged_meta, meta)
            os.replace(staged, target)
        except OSError:
            if previous:
                os.replace(work/"old-source", info)
            else:
                info.unlink()
            if new_meta:
                meta.unlink(missing_ok=True)
            raise
    return {"asset": target.relative_to(project).as_posix(), "source": info.relative_to(project).as_posix(),
            "id": asset_id, "width": size[0], "height": size[1], "sha256": source["asset_sha256"]}


def import_png(project, collection, name, file, creator="", prompt="", model="", original="", processing=""):
    file = Path(file)
    data = file.read_bytes()
    origin = Path(original) if original else file
    original_bytes = origin.read_bytes() if original else data
    source = {"operation": "import", "original": origin.name, "original_sha256": sha256(original_bytes),
              "original_size": png_size(original_bytes)}
    source.update({key: value for key, value in dict(creator=creator,prompt=prompt,model=model,processing=processing).items() if value})
    return save(project, collection, name, data, source)


def generate(project, collection, name, prompt):
    destination(Path(project), collection, name)
    endpoint = os.environ.get("REDFLAG_IMAGE_ENDPOINT", "")
    model = os.environ.get("REDFLAG_IMAGE_MODEL", "")
    address = urlsplit(endpoint)
    if not prompt.strip() or not model or address.scheme not in ("http","https") or not address.hostname:
        raise ValueError("需要提示词、REDFLAG_IMAGE_MODEL 与完整的 REDFLAG_IMAGE_ENDPOINT。")
    body = json.dumps({"model": model, "prompt": prompt, "n": 1}, ensure_ascii=False).encode("utf8")
    request = Request(endpoint, data=body, headers={"Content-Type": "application/json"})
    if token := os.environ.get("REDFLAG_IMAGE_KEY"):
        request.add_unredirected_header("Authorization", "Bearer " + token)
    with urlopen(request, timeout=180) as response:
        response_data = json.load(response)
    try:
        reply = response_data["data"][0]
        data = base64.b64decode(reply["b64_json"], validate=True)
    except (IndexError, KeyError, TypeError, ValueError) as error:
        raise ValueError("生图服务须在 data[0].b64_json 返回 Base64 编码的 PNG。") from error
    source = {"operation": "generate", "provider": address.hostname, "model": model, "prompt": prompt}
    if reply.get("revised_prompt"):
        source["revised_prompt"] = reply["revised_prompt"]
    return save(project, collection, name, data, source)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path, default=PROJECT)
    commands = parser.add_subparsers(dest="command", required=True)
    for verb in ("import","generate"):
        sub = commands.add_parser(verb)
        sub.add_argument("collection")
        sub.add_argument("name")
        sub.add_argument("--prompt", default="", required=verb == "generate")
        if verb == "import":
            sub.add_argument("file")
            for field in ("creator","model","original","processing"):
                sub.add_argument("--"+field, default="")
    args = vars(parser.parse_args())
    operation = args.pop("command")
    result = (import_png if operation=="import" else generate)(**args)
    print(json.dumps(result, ensure_ascii=False))
