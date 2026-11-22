"""编译并运行空项目的 C# 示例，核对模型回复、附图、取消和断线。"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import threading

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"scripts"))
from decision_service import ChatModel, Server


def compile_example():
    output = ROOT/"bin-int/Tests/ModelConnection"
    output.mkdir(parents=True,exist_ok=True)
    locator = Path(os.environ["ProgramFiles(x86)"])/"Microsoft Visual Studio/Installer/vswhere.exe"
    compiler = Path(subprocess.check_output([locator,"-latest","-products","*","-find","MSBuild/**/Roslyn/csc.exe"],
                                            text=True).strip().splitlines()[0])
    framework = Path(os.environ["ProgramFiles(x86)"])/"Reference Assemblies/Microsoft/Framework/.NETFramework/v4.7.2"
    refs = ["/reference:"+str(framework/name) for name in ("System.Net.Http.dll","System.Web.Extensions.dll")]
    library = output/"Redflag-ScriptCore.dll"
    sources = sorted((ROOT/"Redflag-ScriptCore/Source").rglob("*.cs"))
    subprocess.run([compiler,"/nologo","/target:library","/out:"+str(library),*refs,*sources],check=True)
    exe = output/"ModelConnectionTests.exe"
    subprocess.run([compiler,"/nologo","/out:"+str(exe),"/reference:"+str(library),*refs,ROOT/"tests/ModelConnectionTests.cs"],check=True)
    return exe


def main(args):
    exe = compile_example()
    model = ChatModel(args.endpoint,args.model,True)
    with Server(model,0) as service:
        worker = threading.Thread(target=service.serve_forever)
        worker.start()
        try:
            index = ROOT/"output/npc-vision/index.json"
            image = index.parent/json.loads(index.read_bytes())["views"][0]["image"]
            result = subprocess.run([exe,str(service.server_port),image],capture_output=True,timeout=100)
            if result.returncode: raise RuntimeError(result.stderr.decode(errors="replace"))
            texts = result.stdout.decode("utf-8-sig").splitlines()
            mono = ROOT/"Redflag/vendor/mono/lib/Release/mono-sgen.exe"
            env = dict(os.environ, MONO_PATH=str(ROOT/"RedflagForge/mono/lib/mono/4.5"), MONO_CFG_DIR=str(ROOT/"RedflagForge/mono/etc"))
            embedded = subprocess.run([mono,exe,str(service.server_port),image],env=env,capture_output=True,timeout=100)
            if embedded.returncode: raise RuntimeError(embedded.stderr.decode("utf8",errors="replace"))
            mono_texts = embedded.stdout.decode("utf-8-sig").splitlines()
        finally:
            service.shutdown()
            worker.join()
    subprocess.run([exe,str(service.server_port),"--offline"],capture_output=True,timeout=12,check=True)
    data = dict(model=args.model, runtime=".NET Framework 4.7.2 API", image=image.relative_to(ROOT).as_posix(),
                replies=texts, project_mono_6_12_replies=mono_texts, missing_model_rejected=True,
                cancelled=True, recovered_after_cancel=True, offline_reported=True)
    args.out.parent.mkdir(parents=True,exist_ok=True)
    args.out.write_text(json.dumps(data,ensure_ascii=False,indent=2)+"\n",encoding="utf8")
    print(args.out)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--endpoint",default="http://127.0.0.1:18157/v1/chat/completions")
    parser.add_argument("--model",default="Qwen3-VL-2B-Instruct")
    parser.add_argument("--out",type=Path,default=ROOT/"output/validation/runtime-ai/csharp-sdk.json")
    main(parser.parse_args())
