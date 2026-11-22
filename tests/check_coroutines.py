"""运行正式 Mono 脚本，检查跨帧恢复、取消和 JobSystem 的真实工作线程。"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    core = ROOT/"RedflagForge/Resources/Scripts/Redflag-ScriptCore.dll"
    compiler = Path(os.environ["WINDIR"])/"Microsoft.NET/Framework64/v4.0.30319/csc.exe"
    with tempfile.TemporaryDirectory(prefix="redflag-script-") as folder:
        project = Path(folder)
        scripts = project/"Resources/Scripts"
        scripts.mkdir(parents=True)
        shutil.copy2(core,scripts/core.name)
        shutil.copytree(ROOT/"RedflagForge/mono",project/"mono")
        (project/"assets").mkdir()
        (project/"assets/lesson.txt").write_text("机房",encoding="utf8")
        subprocess.run([str(compiler),"/nologo","/target:library",f"/reference:{core}",f"/out:{scripts/'Sandbox.dll'}",
                        str(ROOT/"tests/CoroutineProbe.cs")],check=True)
        run = subprocess.run([str(ROOT/"tests/run_scene_tests.cmd"),"--coroutines",str(project)],
                             cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=90)
        lines = [row.decode("utf8",errors="replace") for row in run.stdout.splitlines()]
        if run.returncode: raise RuntimeError("\n".join(lines[-35:]))
        events = (project/"routine-events.txt").read_text(encoding="utf-8-sig").splitlines()

    rows = [row.split(" ",3) for row in events if row.startswith("1 ")]
    points = {key:int(frame) for _,frame,thread,key in rows}
    assert [(points[key]) for key in ("start","next-frame","half-second","door-open","received-17")] == [0,1,3,6,8],events
    assert "text-机房" in points and "Asset file not found in assets: missing.txt" in points
    assert "task-cancelled" in points and "task-error-offline" in points
    assert len({thread for _,_,thread,_ in rows}) == 1
    assert all(events.count(name) == 1 for name in ("disposed-2","disposed-3"))
    assert sum("cancel-finally" in s for s in events) == sum("self-finally" in s for s in events) == 1
    assert "isolated-failure" in points and not any("unexpected" in s for s in events)
    jobs = [s.split() for s in lines if s.startswith("JOB ")]
    workers = {row[1]:row[3] for row in jobs if row[2]=="worker"}
    assert len(workers)==3 and len(jobs)==6
    assert all(r[4] in workers and workers[r[4]]!=r[3] for r in jobs if r[2]=="main")
    report = ROOT/"output/validation/coroutines/check.json"
    report.parent.mkdir(parents=True,exist_ok=True)
    report.write_text(json.dumps(dict(script_core_sha256=hashlib.sha256(core.read_bytes()).hexdigest(),step_seconds=.25,
        events=events,jobs=[dict(id=int(r[1]),lane=r[2],thread=r[3],depends_on=int(r[4])) for r in jobs],
        native_checks=[s for s in lines if s.startswith("PASS ")]),ensure_ascii=False,indent=2)+"\n",encoding="utf8")
    print(f"PASS Mono game coroutine lifecycle and {len(jobs)} worker/main jobs. {report}")


if __name__=="__main__":
    main()
