"""验证多个编辑器同时写入、作者手改、保存中断以及人工接管时的行为。"""
import json
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import sys
from tempfile import TemporaryDirectory
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"scripts/redflag_mcp"))
import asset_changes
from playtest import append_event, begin, current, frames, issue, run_folder, write_json
from playtest_metrics import assess


def contract(project):
    scene = project/"assets/scenes/room.redflag"
    scene.parent.mkdir(parents=True)
    scene.write_text("Scene: 房间\nSpeed: 1.0 # 米/秒\n",encoding="utf8")
    ui = project/"assets/hud.yaml"
    ui.write_text("Font: 12 # 字号\n",encoding="utf8")
    edits = [{"asset":"assets/scenes/room.redflag","field":["Speed"],"before":1.0,"after":2.5},
             {"asset":"assets/hud.yaml","field":["Font"],"before":12,"after":26}]
    change = asset_changes.propose(project,"更改速度与字号",edits)["id"]
    # 作者先改了同一字段。两个文件都应保留原样，直到作者解决冲突。
    ui.write_text("Font: 18 # 人工编辑\n",encoding="utf8")
    try:
        asset_changes.apply(project,change)
        raise AssertionError("应报告字号冲突")
    except ValueError:
        assert "Speed: 1.0" in scene.read_text("utf8")
    ui.write_text("Font: 12 # 人工编辑\n",encoding="utf8")
    save = asset_changes.replace_bytes
    def fail_second(path,old,new):
        if path == ui:
            raise OSError("磁盘写入中断")
        save(path,old,new)
    with patch.object(asset_changes,"replace_bytes",fail_second):
        try:
            asset_changes.apply(project,change)
            raise AssertionError("应保留保存失败")
        except OSError:
            assert "Speed: 1.0" in scene.read_text("utf8")
    asset_changes.apply(project,change)
    assert ui.read_text("utf8")=="Font: 26 # 人工编辑\n"
    asset_changes.apply(project,change,undo=True)
    assert ui.read_text("utf8")=="Font: 12 # 人工编辑\n"

    meta = begin(project,"assets/scenes/room.redflag","手动接管测试",launch=False,pose_nodes=["hip","foot","hip"])
    folder = run_folder(project,meta["session"])
    assert json.loads((folder/"session.json").read_bytes())["pose_nodes"] == ["hip","foot"]
    with ThreadPoolExecutor(3) as pool:
        commands = list(pool.map(lambda key:issue(folder,1,"tap",key=key),("W","S","E")))
    assert sorted(c["id"] for c in commands)==[1,2,3]
    write_json(folder/"next-command.json",3)
    assert issue(folder,1,"tap",key="R")["id"]==4
    write_json(folder/"handoff.json",{"owner":"human","generation":2})
    for generation in (1,2):
        try:
            issue(folder,generation,"hold",key="W")
            raise AssertionError("不能抢回用户的操作权")
        except ValueError:
            pass
    def frame(seq,events):
        return dict(seq=seq,seconds=seq*.1,frame_ms=10.,actors=[dict(id="2",position=[0.,0.,0.])],inputs=[],events=events)
    for seq,event in enumerate(("enter",None,"complete"),1):
        append_event(folder/"frames.jsonl",frame(seq,[dict(kind=event,entity="2",text="")] if event else []))
    with (folder/"frames.jsonl").open("ab") as stream:
        stream.write(b'{"seq":4')
    assert current(folder)["frame"]["seq"]==3
    first = frames(folder,limit=1)
    assert [f["seq"] for f in frames(folder,first["offset"])["frames"]]==[2,3]
    report = assess(folder,{"actor":"2","checks":{"required_channels":["audio"],
                         "transitions":[{"from":"enter","to":"complete","within_s":.12}]}})
    assert {e["kind"] for e in report["findings"]}=={"late_transition","missing_channel"}


def direct_input(project):
    folder = project / "direct"
    folder.mkdir()
    profile = {"actor": "2", "actions": {"walk": {"key": "W"}},
               "checks": {"movement": {"walk": .15}}}

    def frame(seq, x, op="", id=0, owner="agent", generation=1):
        sample = [{"op": op, "owner": owner, "key": "W", "id": id}] if op else []
        append_event(folder / "frames.jsonl", dict(seq=seq, seconds=seq*.1, frame_ms=10, generation=generation,
                     actors=[{"id":"2","position":[x,0,0]}], inputs=sample, events=[]))

    frame(1, 0.)
    frame(2, .1, "hold", 1)
    frame(3, .5)
    frame(4, .5, "release", 2)
    assert assess(folder, profile)["measured_moves_m"] == [.5]

    # 有规划器记录时不能把相同的按下命令再算一次。玩家接管后的移动也不能算给 Agent。
    append_event(folder/"decisions.jsonl", {"choice":"walk", "input":1, "from":1, "to":4})
    frame(5, .5, "hold", 3)
    frame(6, 1.5, "release", owner="human", generation=2)
    frame(7, 1.5, "release", 4, generation=3)
    frame(8, 1.5, "tap", 5, generation=3)
    report = assess(folder, profile)
    assert report["measured_moves_m"] == [.5, 0.]
    assert [f["kind"] for f in report["findings"]] == ["no_progress"]

    # 两种动作使用同一个键时，原始按键无法确定动作名。
    profile["actions"]["confirm"] = {"key":"W"}
    (folder/"decisions.jsonl").unlink()
    assert [f["kind"] for f in assess(folder, profile)["findings"]] == ["movement_not_observed"]


if __name__=="__main__":
    with TemporaryDirectory() as work:
        contract(Path(work))
        direct_input(Path(work))
    print("PASS cross-asset conflict, rollback, undo, concurrent input, human ownership, partial frame and timeline checks")
