"""用本机关卡和已启动的真实模型检查 NPC 对场地与灯具的描述。"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import time
import yaml
from check_decision_service import ROOT, call


def describe(url, records):
    question = "说明我现在所在的区域。用两句简短中文讲记录中的地貌、植被、地标和灯具开关。有哪项说哪项，记录里没有就不要添加。如果没有区域记录，只回答“当前位置没有区域记录”。"
    body = dict(instructions="你是游戏内的巡检员。请忠实转述当前区域的数据，每份输入独立。",
                prompt=question + "\n引擎在当前位置的记录：" + json.dumps(records, ensure_ascii=False))
    job = url + "/v1/replies/" + call(url + "/v1/replies", "POST", body)["id"]
    end = time.monotonic() + 90
    try:
        while time.monotonic() < end:
            result = call(job)
            if result["status"] != "pending":
                assert result["status"] == "completed", result
                return result["text"]
            time.sleep(.05)
        raise TimeoutError("模型没有在 90 秒内回答。")
    finally:
        call(job, "DELETE")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8716)
    args = parser.parse_args()
    src = ROOT / "RedflagForge/assets/scenes/Watt-Station.redflag"
    raw = src.read_bytes()
    scene = yaml.safe_load(raw)
    # 这个宿主不需要图形设备。保留原始区域、变换、ID、名字及灯光组件，其他网格仍由正式 Forge 绘制。
    kept = ("Entity", "TagComponent", "TransformComponent", "EnvironmentZoneComponent", "PointLightComponent", "DirectionalLightComponent")
    entities = [{k:v for k,v in e.items() if k in kept} for e in scene["Entities"]
                if any(k in e for k in kept[3:])]
    with tempfile.TemporaryDirectory(prefix="redflag-station-zones-") as directory:
        work = Path(directory)
        (work / "station-zones.redflag").write_text(yaml.safe_dump(dict(Scene=scene["Scene"], Entities=entities),
                                                   sort_keys=False, allow_unicode=True), encoding="utf8")
        subprocess.run([str(ROOT / "tests/run_scene_tests.cmd"), "--npc-environment", "station", str(work)], check=True)
        sensed = yaml.safe_load((work / "sensed.yaml").read_text("utf8"))
    url = f"http://127.0.0.1:{args.port}"
    model = call(url + "/v1/health")["model"]
    answers = {str(k):dict(input=v, reply=describe(url,v)) for k,v in sensed.items()}
    report = ROOT / "output/validation/runtime-ai/station-environment-model.json"
    report.write_text(json.dumps(dict(scene_sha256=hashlib.sha256(raw).hexdigest(), model=model, views=answers),
                                 ensure_ascii=False, indent=2) + "\n", encoding="utf8")
    print(json.dumps({k:row["reply"] for k,row in answers.items()}, ensure_ascii=False))


if __name__ == "__main__":
    main()
