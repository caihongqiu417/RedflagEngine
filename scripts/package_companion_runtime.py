"""给 Windows 试玩包配一份离线队友运行环境。将生成的 ZIP 与游戏解压到同一目录。"""
import argparse
import hashlib
import json
from importlib import metadata
from pathlib import Path
from tempfile import TemporaryDirectory
from zipfile import ZIP_DEFLATED, ZIP_STORED, ZipFile

ROOT = Path(__file__).resolve().parents[1]
MODELS = {
    "Qwen3VL-2B-Instruct-Q4_K_M.gguf": "089d75c52f4b7ffc56ba998ffc50aae89fcafc755f9e7208aacca281dca6c2ae",
    "mmproj-Qwen3VL-2B-Instruct-Q8_0.gguf": "f9a68fabba69c3b81e153367b2c7521030b0fa8bb0de400c9599c8e6725f9c82",
}


def build(args):
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with TemporaryDirectory(dir=args.output.parent) as work:
        archive = Path(work) / "runtime.zip"
        with ZipFile(archive, "w", ZIP_DEFLATED, compresslevel=3) as out:
            def add(file, name):
                compression = ZIP_STORED if file.suffix == ".gguf" else ZIP_DEFLATED
                out.write(file, "CompanionRuntime/" + name, compress_type=compression)

            for name, expected in MODELS.items():
                file = args.model_dir / name
                with file.open("rb") as src:
                    if hashlib.file_digest(src, "sha256").hexdigest() != expected:
                        raise ValueError(f"模型与所选的 Qwen3-VL-2B 发布版本不符：{file}")
                add(file, "models/" + name)
            add(ROOT / "LICENSE", "models/Apache-2.0.txt")
            add(args.llama_license, "llama/LICENSE")
            for file in sorted(args.llama_dir.iterdir()):
                if file.suffix == ".dll" or file.name.startswith("LICENSE-") or file.name == "llama-server.exe":
                    add(file, "llama/" + file.name)

            add(args.speech / "DeliverySpeechRuntime.dll", "speech/DeliverySpeechRuntime.dll")
            for subdir in ("models", "licenses"):
                for file in sorted((args.speech / subdir).rglob("*")):
                    if file.is_file():
                        add(file, "speech/" + file.relative_to(args.speech).as_posix())
            add(args.speech / "runtime_manifest.json", "speech/runtime_manifest.json")

            # 嵌入版解释器随包移动。最后一行路径让语音服务找到游戏 scripts 中的相邻模块。
            with ZipFile(args.python_embed) as embed:
                for name in embed.namelist():
                    if name == "python313._pth":
                        data = b"python313.zip\n.\nLib/site-packages\n../../scripts\nimport site\n"
                    else:
                        data = embed.read(name)
                    out.writestr("CompanionRuntime/python/" + name, data)

            # 使用专供语音服务的 venv。只复制各安装包登记的文件，同时带上原许可和第三方声明。
            site = args.speech_site.resolve()
            packages = {}
            installed = {}
            for dist in metadata.distributions(path=[str(site)]):
                name = dist.metadata["Name"]
                if name.lower() in {"pip", "wheel", "setuptools"}:
                    continue
                packages[name] = dist.version
                for entry in dist.files:
                    file = Path(dist.locate_file(entry)).resolve()
                    if file.is_relative_to(site) and file.is_file() and "__pycache__" not in file.parts:
                        installed[file] = file.relative_to(site).as_posix()
            for file, relative in sorted(installed.items()):
                add(file, "python/Lib/site-packages/" + relative)
            sources = {
                "model": "https://huggingface.co/Qwen/Qwen3-VL-2B-Instruct-GGUF",
                "model_sha256": MODELS,
                "llama": "https://github.com/ggml-org/llama.cpp/releases/tag/b10887",
                "python": "https://www.python.org/downloads/release/python-31312/",
                "python_packages": packages,
                "speech": "speech/runtime_manifest.json",
            }
            out.writestr("CompanionRuntime/sources.json", json.dumps(sources, indent=2))
        archive.replace(args.output)
    print(args.output)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for flag in ("output", "model-dir", "llama-dir", "llama-license", "speech", "python-embed", "speech-site"):
        parser.add_argument("--" + flag, type=Path, required=True)
    build(parser.parse_args())
