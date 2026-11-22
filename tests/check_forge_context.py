"""在 Forge 控件中查找接口、打开匹配项并留下勘误，用同一份 CLI 任务记录验证。"""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from capture_creation_review import capture
from profile_native_navigation import ROOT


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=ROOT/"bin/Debug-windows-x86_64/RedflagForge/RedflagForge.exe")
    parser.add_argument("--check-indices", type=Path, help="对验证用网格逐个检查三角形的索引")
    args = parser.parse_args()
    if args.check_indices:
        indices = [int(s) for s in args.check_indices.read_text().splitlines()]
        for start in range(0, len(indices), 3):
            part = indices[start:start + 3]
            print(f"triangle {start // 3:03}: {part}", flush=True)
            if len(part) != 3:
                print("FAIL triangle needs 3 indices, got", len(part))
                return 1
        print(f"PASS {len(indices) // 3} complete triangles")
        return 0
    scratch = Path(tempfile.mkdtemp(prefix="dev-context-ui-", dir=ROOT/"tmp"))
    project = scratch/"original"
    project.mkdir()
    with tempfile.TemporaryDirectory(prefix="forge-context-", dir=ROOT/"tests") as fixture:
        run_case(args.exe, scratch, project, Path(fixture)/"indices.txt")
    return 0


def run_case(exe, scratch, project, indices):
    command = [sys.executable, str(ROOT/"scripts/dev_context.py"), "--project", str(project)]
    def cli(*values):
        return json.loads(subprocess.check_output([*command, *values], cwd=ROOT))
    name = indices.relative_to(ROOT).as_posix()
    def set_indices(count):
        indices.write_text("".join(f"{n}\n" for n in range(count)),encoding="utf8")
    set_indices(300)
    task = cli("start", "检查网格索引与创建网格的调用", "--files", "Redflag/src/Redflag/Renderer/RHI/RenderDevice.h", name)["task"]
    verify = [*command,"verify",task,"--",sys.executable,"tests/check_forge_context.py","--check-indices",name]
    passed = json.loads(subprocess.check_output(verify,cwd=ROOT))
    set_indices(331)
    failed_run = subprocess.run(verify,cwd=ROOT,capture_output=True)
    failed = json.loads(failed_run.stdout)
    assert failed_run.returncode == failed["exit_code"] == 1
    moved = scratch/"moved-project"
    shutil.copytree(project,moved)
    check = subprocess.run([*command, "verify", task, "--", str(Path("tests")/"run_scene_tests.cmd"), "--dev-context-panel", str(project), task, str(moved)],
                           cwd=ROOT, capture_output=True, timeout=100)
    recorded = json.loads(check.stdout)
    out = Path(recorded["log"]).read_text(encoding="utf8", errors="replace")
    if check.returncode: raise RuntimeError(f"{recorded['log']}\n{out[-1300:]}")
    review = cli("review", task)
    assert len(review["notes"]) == 1 and [r["operation"] for r in review["references"]] == ["find","show","show"]
    assert [(r["exit_code"],r["current"]) for r in review["checks"]] == [(0,False),(1,True),(0,True)]
    item = next(r for r in review["references"] if r["operation"] == "show")
    found = cli("show", item["query"])
    assert found.get("tests") and found.get("implementation"),found

    output = ROOT/"output/validation/forge-context"
    output.mkdir(parents=True,exist_ok=True)
    binary = scratch/"RedflagForge.exe"
    shutil.copyfile(exe, binary)
    ctypes.windll.user32.SetProcessDPIAware()
    capture(binary, "", "dev-context", project, output/"record.png", {"REDFLAG_DEV_TASK":task})
    capture(binary, "", "dev-context", project, output/"source.png", {"REDFLAG_DEV_SYMBOL":item["query"]})
    artifacts = {}
    for field, title, dest in (("log","log.png","indices-output.txt"),("diff","diff.png","indices.diff")):
        source = Path(failed[field])
        rel = source.relative_to(project/"captures/context"/task).as_posix()
        capture(binary,"","dev-context",moved,output/title,{"REDFLAG_DEV_TASK":task,"REDFLAG_DEV_EVIDENCE":rel})
        shutil.copyfile(source,output/dest)
        artifacts[field] = {"file":dest,"screenshot":title,"bytes":source.stat().st_size,
                            "sha256":hashlib.sha256(source.read_bytes()).hexdigest()}
    data = dict(checks=[line[5:] for line in out.splitlines() if line.startswith("PASS ")], notes=review["notes"],
                references=[{"operation":r["operation"],"query":r["query"],"sources":r["sources"]} for r in review["references"]],
                implementation=found["implementation"], tests=found["tests"], check_exit_code=recorded["exit_code"],
                task=task, checks_at_review=[{"exit":r["exit_code"],"same_source":r["current"]} for r in review["checks"]],
                snapshots=artifacts,project_copy_read=True,
                exe_sha256=hashlib.sha256(binary.read_bytes()).hexdigest())
    (output/"check.json").write_text(json.dumps(data,ensure_ascii=False,indent=2)+"\n",encoding="utf8")
    print(f"PASS Forge and CLI read the same task. {output}")


if __name__=="__main__":
    raise SystemExit(main())
