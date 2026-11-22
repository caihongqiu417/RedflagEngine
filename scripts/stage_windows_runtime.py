"""将 Release 游戏实际导入的 x64 DLL 放在 exe 旁，生成可交给 asset_bundle.py 的目录。"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
REDIST_LIST = "https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution"
WINDOWS = set("advapi32 bcrypt crypt32 comdlg32 gdi32 imm32 kernel32 ole32 oleaut32 shell32 user32 "
              "version winhttp winmm ws2_32".split())


def tools():
    finder = Path(os.environ["ProgramFiles(x86)"])/"Microsoft Visual Studio/Installer/vswhere.exe"
    vs = Path(subprocess.check_output([finder,"-latest","-products","*","-requires","Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                                       "-property","installationPath"],text=True).strip())
    versions = [p for p in (vs/"VC/Redist/MSVC").iterdir() if p.name[:1].isdigit()]
    redist = max(versions,key=lambda p: tuple(map(int,p.name.split("."))))/"x64/Microsoft.VC143.CRT"
    dump = sorted((vs/"VC/Tools/MSVC").glob("*/bin/Hostx64/x64/dumpbin.exe"))[-1]
    return redist,dump


def imports(dump, file):
    # PE 头直接给出架构。只从 dumpbin 读取依赖名，整个检查都不启动待发布的程序。
    with file.open("rb") as stream:
        stream.seek(0x3c)
        offset, = struct.unpack("<I",stream.read(4))
        stream.seek(offset)
        if stream.read(6) != b"PE\x00\x00\x64\x86":
            raise ValueError(f"需要 x64 Windows 程序或 DLL：{file}")
    report = subprocess.check_output([dump,"/nologo","/dependents",file]).decode("ascii",errors="replace")
    return sorted({name.lower() for name in re.findall(r"^\s+([^\s/\\]+\.dll)\s*$",report,re.I|re.M)})


def stage(exe, destination):
    redist,dump = tools()
    if destination.exists() and any(destination.iterdir()):
        raise ValueError("请选择空目录，避免把上一次留下的 DLL 打入新包。")
    destination.parent.mkdir(parents=True,exist_ok=True)
    pending = [(exe,"Release build")]
    files, sources = {}, {}
    while pending:
        path,source = pending.pop()
        name = path.name.lower()
        if name in files: continue
        needed = imports(dump,path)
        for dep in needed:
            if dep == "ucrtbased.dll" or re.fullmatch(r"(msvcp|vcruntime|concrt)\d+(_\d+)?d\.dll|shaderc_sharedd\.dll",dep):
                raise ValueError(f"{path.name} 使用调试库 {dep}，请重新构建 Release。")
            if dep == "vulkan-1.dll" or dep.startswith(("api-ms-win-","ext-ms-win-")) or Path(dep).stem in WINDOWS:
                continue
            if (redist/dep).is_file():
                pending.append((redist/dep,"Microsoft.VC143.CRT "+redist.parent.parent.name))
            elif (exe.parent/dep).is_file():
                pending.append((exe.parent/dep,"Release build"))
            else:
                raise FileNotFoundError(f"{path.name} 还需要 {dep}")
        sources[name] = path
        files[name] = dict(name=path.name,source=source,imports=needed)
    with tempfile.TemporaryDirectory(dir=destination.parent) as folder:
        stage = Path(folder)/"runtime"
        stage.mkdir()
        for name,path in sources.items():
            target = stage/path.name
            shutil.copyfile(path,target)
            files[name]["sha256"] = hashlib.sha256(target.read_bytes()).hexdigest()
        if destination.exists(): destination.rmdir()  # 只接受调用者选定的空输出目录。
        stage.rename(destination)
    return dict(architecture="Windows x64",system="Windows 10/11, Vulkan 1.3 graphics driver",
                redist_list=REDIST_LIST,files=list(files.values()))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory",type=Path)
    parser.add_argument("--exe",type=Path,default=ROOT/"bin/Release-windows-x86_64/RedflagForge/RedflagForge.exe")
    parser.add_argument("--report",type=Path)
    args = parser.parse_args()
    report = stage(args.exe.resolve(strict=True),args.directory)
    if args.report:
        args.report.parent.mkdir(parents=True,exist_ok=True)
        args.report.write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n",encoding="utf8")
    print(args.directory/args.exe.name)
