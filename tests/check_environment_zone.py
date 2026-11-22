"""在仓库根目录运行 python tests/check_environment_zone.py。"""
import json
import tempfile
from check_npc_memory import ROOT, run_reader


class FieldReader:
    name = "zone-observation-test"
    images = False

    def __init__(self):
        self.frames = {}

    def choose(self, request, cancel):
        self.frames[request["prompt"]] = {f["id"]: f["description"] for f in request["observations"] if f["id"].startswith("zone/")}
        return "revisit"


def main():
    model = FieldReader()
    with tempfile.TemporaryDirectory(prefix="redflag-zone-") as temp:
        output, _, core = run_reader(model, temp, "--npc-environment", ROOT / "tests/EnvironmentProbe.cs")
    stages = model.frames
    boundary, lamp, outer = "zone/90/", "zone/90/light/91", "zone/93/name"
    inside = stages["inside"]
    assert len(inside) == 7 and "废弃栈桥" in inside[boundary + "name"]
    assert "河谷阶地" in inside[boundary + "landform"] and "芦苇" in inside[boundary + "vegetation"]
    assert "旧泵房" in inside[boundary + "landmark"] and "关联方向光" in inside[boundary + "light/92"]
    assert "已开启" in inside[lamp] and "距角色 2 米" in inside[lamp] and "强度 3" in inside[lamp]
    assert "无光输出" in stages["lamp-off"][lamp] and "已开启" not in stages["lamp-off"][lamp]
    assert lamp not in stages["lamp-removed"] and len(stages["lamp-removed"]) == 6
    assert set(stages["outside-inner"]) == {outer} and not stages["outside-all"] and not stages["disabled"]
    assert "ZONE 7 " in output and "ZONE 6 no-lamp" in output and "ZONE 1 no-lamp" in output and "ZONE 0 no-lamp" in output
    report = ROOT / "output/validation/runtime-ai/environment-zone.json"
    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text(json.dumps(dict(managed_sha256=core, observations=stages, script=[row for row in output.splitlines()
                                if row.startswith("ZONE ")]), ensure_ascii=False, indent=2) + "\n", encoding="utf8")


if __name__ == "__main__":
    main()
