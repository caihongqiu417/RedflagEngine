"""在空项目制作一个字段修改，交给正式 ImGui 按钮应用、撤销，并在实时会话测试接管和批注。"""
import json
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"scripts/redflag_mcp"))
import asset_changes
import playtest

if __name__=="__main__":
    with TemporaryDirectory(prefix="review-controls-",dir=ROOT/"tmp") as name:
        project=Path(name)
        scenes=project/"assets/scenes"
        scenes.mkdir(parents=True)
        (scenes/"room.redflag").write_text("Scene: Room\nEntities: []\n",encoding="utf8")
        asset=project/"assets/presentation.yaml"
        asset.write_text("Brightness: 18\nCaption: 初稿\n",encoding="utf8")
        change=asset_changes.propose(project,"调整房间亮度",[{"asset":"assets/presentation.yaml","field":["Brightness"],"before":18,"after":30}])
        asset.write_text("Brightness: 18\nCaption: 人工修改\n",encoding="utf8")
        session=playtest.begin(project,"assets/scenes/room.redflag","检查主相机",launch=False)["session"]
        proc=subprocess.run([str(ROOT/"tests/run_scene_tests.cmd"),"--review-panel",str(project),change["id"],session],cwd=ROOT,
                            stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        output=proc.stdout.decode("utf8",errors="replace")
        print(output)
        proc.check_returncode()
        passed=[line[5:] for line in output.splitlines() if line.startswith("PASS ")]
        assert len(passed)>=11,passed
        data={"checks":passed,"feedback":json.loads((project/"captures/playtests"/session/"feedback.jsonl").read_text("utf8"))}
        assert data["feedback"]["entity"]=="101",data
        (ROOT/"output/validation/creation-review/controls.json").write_text(json.dumps(data,ensure_ascii=False,indent=2)+"\n",encoding="utf8")
