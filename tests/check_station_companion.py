"""运行维修站原生角色用例，再在正式 Forge 的 160 帧中核对无指令跟随。"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from urllib.request import urlopen

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / "RedflagForge"
OUT = ROOT / "output/validation/station-companion"


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def save(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def run_cases(port, exe, work, names):
    check = ROOT / "bin-int/Tests/StationCompanionTests.exe"
    env = dict(os.environ, PATH=str(exe.parent) + os.pathsep + os.environ["PATH"])
    cases = {}
    for case in names:
        if case == names[0]:
            command = [os.environ["COMSPEC"], "/d", "/c", "tests\\run_scene_tests.cmd", "--station-companion", str(port), case]
        else:
            command = [check, "--station-companion", str(port), case]
        result = subprocess.run(command, cwd=ROOT if case == names[0] else PROJECT, env=env, capture_output=True, timeout=95)
        log = (result.stdout + result.stderr).decode("utf-8", errors="replace")
        (work / (case + ".log")).write_text(log, encoding="utf-8")
        if result.returncode:
            raise RuntimeError(f"{case}: {work / (case + '.log')}\n{log[-1000:]}")
        events, checks, distances = [], [], {}
        for line in log.splitlines():
            if line.startswith("PASS "):
                checks.append(line[5:])
            elif line.startswith("EVENT\t"):
                _, seq, kind, text, target = line.split("\t")
                events.append(dict(seq=int(seq), kind=kind, text=text, target=target))
            elif line.startswith("WATCH\t"):
                _, frame, kind, text, target, speech = line.split("\t")
                events.append(dict(game_step=int(frame), kind=kind, text=text, target=target, speech=speech))
            elif line.startswith(("DISTANCE\t", "WORLD\t")):
                distances = dict(item.split("=") for item in line.split("\t")[2:])
        cases[case] = dict(checks=checks, events=events, measurements=distances)
    return cases, digest(check)


def run_game(exe, work):
    folder = work / "game"
    folder.mkdir()
    save(folder / "session.json", {"scene": "assets/scenes/Watt-Station.redflag", "intent": "玩家沉默时跟随队友"})
    save(folder / "handoff.json", {"generation": 1, "owner": "human"})
    # 运行独立的二进制副本，以便 Forge 的正式输出能继续构建。
    game = work / exe.name
    shutil.copy2(exe, game)
    env = dict(os.environ, REDFLAG_PLAYTEST=str(folder), PATH=str(exe.parent) + os.pathsep + os.environ["PATH"])
    with (folder / "forge.log").open("wb") as log:
        subprocess.run([game, "--vulkan", "--play", "assets/scenes/Watt-Station.redflag", "--hidden", "--frames", "160"],
                       cwd=PROJECT, env=env, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=180)
    frames = [json.loads(line) for line in (folder / "frames.jsonl").read_bytes().splitlines()]
    starts, ends = ({a["id"]: a for a in row["actors"]} for row in (frames[0], frames[-1]))
    before, after = (math.dist(group["1102"]["position"], group["1101"]["position"]) for group in (starts, ends))
    events = [e for row in frames for e in row["events"] if e["entity"] == "1102"]
    assert len(frames) == 160 and after < before - .1 and ends["1102"]["action"] == "follow"
    assert any(e["kind"] == "队伍约定" for e in events)
    return dict(exe_sha256=digest(game), frames=len(frames), actor="1102", leader="1101", events=events,
                first=starts["1102"], last=ends["1102"], distance_before_m=before, distance_after_m=after)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8716)
    parser.add_argument("--exe", type=Path, default=ROOT / "bin/Debug-windows-x86_64/RedflagForge/RedflagForge.exe")
    parser.add_argument("--watch", action="store_true", help="用正在运行的巡检机脚本检查主动观察、静默偏好、遮挡和过时的报点")
    args = parser.parse_args()
    exe = args.exe.resolve(strict=True)
    (ROOT / "tmp").mkdir(exist_ok=True)
    work = Path(tempfile.mkdtemp(dir=ROOT / "tmp", prefix="station-companion-"))
    with urlopen(f"http://127.0.0.1:{args.port}/v1/health", timeout=5) as response:
        model = json.load(response)["model"]
    cases = ("watch-announce", "watch-quiet", "watch-wall", "watch-back", "watch-lost") if args.watch else ("race", "deliver", "reserve")
    results, host = run_cases(args.port, exe, work, cases)
    OUT.mkdir(parents=True, exist_ok=True)
    save(OUT / ("watch.json" if args.watch else "decisions.json"),
         dict(model=model, host_sha256=host, profile_sha256=digest(PROJECT / "assets/ai/watt.npc.yaml"),
              tree_sha256=digest(PROJECT / "assets/ai/watt.tree.yaml"), cases=results))
    if args.watch:
        print(f"PASS {len(cases)} observation cases. Output: {OUT}. Full logs: {work}")
        return
    save(OUT / "game.json", run_game(exe, work))
    print(f"PASS 3 model scenarios and 160 game frames. Output: {OUT}. Full session: {work}")


if __name__ == "__main__":
    main()
