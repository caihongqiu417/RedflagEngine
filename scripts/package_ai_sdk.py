"""从正式 ScriptCore 工程生成 Windows AI Runtime SDK。发布清单不扫描用户目录。"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from zipfile import ZIP_DEFLATED, ZipFile

ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = ("Start-NpcServices.ps1","decision_service.py","speech_runtime.py","speech_service.py",
           "speech_synthesize.ps1","speech_room.html","speech_client.py","requirements-speech.txt","qwen_speech.py","voice_bank.py")


def copy(source, target):
    target.parent.mkdir(parents=True,exist_ok=True)
    shutil.copyfile(ROOT/source,target)


def vs_tool(pattern):
    locator = Path(os.environ["ProgramFiles(x86)"])/"Microsoft Visual Studio/Installer/vswhere.exe"
    found = subprocess.check_output([locator,"-latest","-products","*","-find",pattern],text=True).strip().splitlines()
    if not found: raise RuntimeError("请安装 Visual Studio 与 .NET Framework 4.7.2 开发包")
    return found[0]


def build(archive):
    archive.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="redflag-ai-sdk-") as tmp:
        stage = Path(tmp)
        package = stage/"Redflag-AI-Runtime"
        package.mkdir()
        dll = "Redflag-ScriptCore.dll"
        # 使用正式项目的 Release 设置，将编译中间文件留在本次临时目录。
        built = stage/"build"
        subprocess.run([vs_tool("MSBuild/**/Bin/MSBuild.exe"),str(ROOT/"Redflag-ScriptCore/Redflag-ScriptCore.csproj"),
                        "/t:Rebuild","/p:Configuration=Release","/p:Platform=x64","/p:DebugSymbols=false","/p:DebugType=None",
                        "/p:OutputPath="+str(built)+os.sep,"/p:IntermediateOutputPath="+str(stage/"obj")+os.sep,
                        "/nologo","/v:minimal"],check=True)
        shutil.copyfile(built/dll,package/dll)
        copy("examples/ModelConsole/Program.cs", package/"examples/Program.cs")
        for name in ("MemoryHost.cpp", "SurveyNotes.cs", "surveyor.npc.yaml", "surveyor.tree.yaml"):
            copy("examples/npc-memory/"+name, package/"examples/npc-memory"/name)
        subprocess.run([vs_tool("MSBuild/**/Roslyn/csc.exe"),"/nologo","/optimize+","/reference:"+str(package/dll),
                        "/out:"+str(package/"ModelConsole.exe"),package/"examples/Program.cs"],check=True)
        for name in SCRIPTS: copy("scripts/"+name,package/"scripts"/name)
        for name in ("ModelImage","ModelConnection"):
            copy(f"Redflag-ScriptCore/Source/AI/{name}.cs",package/f"source/{name}.cs")
        copy("LICENSE",package/"LICENSE")
        copy("Redflag/vendor/mono/LICENSE",package/"licenses/Mono-LICENSE")
        copy("RedflagForge/mono/etc/mono/4.5/machine.config",package/"mono/etc/mono/4.5/machine.config")
        copy("docs/AI-SDK-Package.md",package/"README.md")

        # 仓库文档带有引擎源码链接；独立包把已有文件指向本地，保留外部资料入口。
        guide = (ROOT/"docs/Model-Connection.md").read_text(encoding="utf8")
        links = {"ModelConnection.cs":"source/ModelConnection.cs","ModelImage.cs":"source/ModelImage.cs",
                 "ModelConnectionTests.cs":"examples/Program.cs","Start-NpcServices.ps1":"scripts/Start-NpcServices.ps1"}
        def local_link(match):
            label, path = match.groups()
            if path.startswith("https://"): return match.group()
            dest = links.get(Path(path).name)
            return f"[{label}]({dest})" if dest else label
        (package/"Model-Connection.md").write_text(re.sub(r"\[([^\]]+)\]\(([^)]+)\)",local_link,guide),encoding="utf8")
        files = {f.relative_to(package).as_posix():hashlib.sha256(f.read_bytes()).hexdigest()
                 for f in sorted(package.rglob("*")) if f.is_file()}
        (package/"manifest.json").write_text(json.dumps({"sdk":"Redflag AI Runtime","protocol":1,"files":files},indent=2)+"\n",encoding="utf8")
        temp_zip = archive.with_suffix(".tmp")
        with ZipFile(temp_zip,"w",ZIP_DEFLATED) as out:
            for name in [*files,"manifest.json"]:
                out.write(package/name,"Redflag-AI-Runtime/"+name)
        temp_zip.replace(archive)
    print(archive)


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--out",type=Path,default=ROOT/"output/releases/Redflag-AI-Runtime-SDK.zip")
    build(p.parse_args().out.resolve())
