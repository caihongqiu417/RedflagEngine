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
    args = parser.parse_args()
    project = Path(tempfile.mkdtemp(prefix="dev-context-ui-", dir=ROOT/"tmp"))
    command = [sys.executable, str(ROOT/"scripts/dev_context.py"), "--project", str(project)]
    def cli(*values):
        return json.loads(subprocess.check_output([*command, *values], cwd=ROOT))
    task = cli("start", "核对创建网格的调用和测试", "--files", "Redflag/src/Redflag/Renderer/RHI/RenderDevice.h")["task"]
    check = subprocess.run([*command, "verify", task, "--", str(Path("tests")/"run_scene_tests.cmd"), "--dev-context-panel", str(project), task],
                           cwd=ROOT, capture_output=True, timeout=100)
    recorded = json.loads(check.stdout)
    out = Path(recorded["log"]).read_text(encoding="utf8", errors="replace")
    if check.returncode: raise RuntimeError(f"{recorded['log']}\n{out[-1300:]}")
    review = cli("review", task)
    assert len(review["notes"]) == 1 and [r["operation"] for r in review["references"]] == ["find","show"]
    assert review["checks"][0]["exit_code"] == 0 and review["checks"][0]["current"]
    item = next(r for r in review["references"] if r["operation"] == "show")
    found = cli("show", item["query"])
    assert found.get("tests") and found.get("implementation"),found

    output = ROOT/"output/validation/forge-context"
    output.mkdir(parents=True,exist_ok=True)
    binary = project/"RedflagForge.exe"
    shutil.copyfile(args.exe, binary)
    ctypes.windll.user32.SetProcessDPIAware()
    capture(binary, "", "dev-context", project, output/"record.png", {"REDFLAG_DEV_TASK":task})
    capture(binary, "", "dev-context", project, output/"source.png", {"REDFLAG_DEV_SYMBOL":item["query"]})
    data = dict(checks=[line[5:] for line in out.splitlines() if line.startswith("PASS ")], notes=review["notes"],
                references=[{"operation":r["operation"],"query":r["query"],"sources":r["sources"]} for r in review["references"]],
                implementation=found["implementation"], tests=found["tests"], check_exit_code=recorded["exit_code"],
                exe_sha256=hashlib.sha256(binary.read_bytes()).hexdigest())
    (output/"check.json").write_text(json.dumps(data,ensure_ascii=False,indent=2)+"\n",encoding="utf8")
    print(f"PASS Forge and CLI read the same task. {output}")


if __name__=="__main__":
    main()
