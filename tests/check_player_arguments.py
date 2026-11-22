"""检查正式 EXE 的无效参数，并用记录实参的小程序核对两个 .cmd 启动器。"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def check(exe):
    with tempfile.TemporaryDirectory(prefix="redflag-arguments-") as folder:
        project = Path(folder)
        scripts = project/"scripts"
        scripts.mkdir()
        for name,target in (("Play-Bundle.cmd","Play.cmd"),("Play-With-Companion.cmd","Play-With-Companion.cmd"),
                            ("Play-Bundle.ps1","scripts/Play-Bundle.ps1")):
            shutil.copyfile(ROOT/"scripts"/name,project/target)
        scene = "assets/scenes/Station fixture.redflag"
        (project/"asset-manifest.json").write_text(json.dumps(dict(scene=scene)))
        (scripts/"Start-NpcServices.ps1").write_text("param([switch]$RequireAll)\nSet-Content 'services.txt' ready\n")
        (project/"Recorder.cs").write_text('using System.IO; class Recorder { static void Main(string[] args) { File.WriteAllLines("args.txt",args); } }')
        compiler = Path(os.environ["WINDIR"])/"Microsoft.NET/Framework64/v4.0.30319/csc.exe"
        subprocess.run([str(compiler),"/nologo","/out:RedflagForge.exe","Recorder.cs"],cwd=project,check=True)
        checks = []
        for launcher in ("Play.cmd","Play-With-Companion.cmd"):
            for options,valid in (([],True),(["--render-size","1280x720"],True),(["--render-size","1920x1080"],True),
                    (["--render-size","800x600"],False),(["--render-size"],False),(["--quality","low"],False),
                    (["--render-size","1280x720","extra"],False)):
                files = [project/"args.txt",project/"services.txt"]
                for file in files: file.unlink(missing_ok=True)
                run = subprocess.run(["cmd","/d","/c",launcher,*options],cwd=project,capture_output=True)
                assert run.returncode == (0 if valid else 2),(launcher,options,run.returncode)
                forwarded = files[0].read_text(encoding="utf-8-sig").splitlines() if files[0].exists() else []
                assert forwarded == (["--vulkan","--play",scene,*options] if valid else []),forwarded
                assert files[1].exists()==(valid and launcher=="Play-With-Companion.cmd")
                checks.append(dict(launcher=launcher,options=options,exit=run.returncode,forwarded=forwarded))
        rejected = []
        for options in (["--play",scene,"--render-size"],["--play",scene,"--render-size","800x600"],
                        ["--render-size","1280x720","--scene",scene],["--play",scene,"--render-size","1920x1080","--render-size","1280x720"]):
            run = subprocess.run([str(exe),*options],capture_output=True)
            assert run.returncode==2,(run.returncode,options)
            rejected.append(dict(options=options,exit=run.returncode,output=run.stdout.decode(errors="replace").strip()))
    return dict(launchers=checks,exe_rejected=rejected)


if __name__=="__main__":
    import sys
    result=check(Path(sys.argv[1]).resolve(strict=True))
    target=ROOT/"output/validation/packaged-watt/resolution-arguments.json"
    target.parent.mkdir(parents=True,exist_ok=True)
    target.write_text(json.dumps(result,indent=2)+"\n",encoding="utf8")
    print(f"PASS {len(result['launchers'])} launcher cases and {len(result['exe_rejected'])} native rejections")
