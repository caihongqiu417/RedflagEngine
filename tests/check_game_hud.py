"""在独立桌面截取正式 --play 的一帧，测 UI 在最终输出图上的绘制次数与覆盖。"""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import yaml
from capture_material_studio import run_renderdoc
from profile_native_navigation import ROOT, prepare
from measure_hud_coverage import compare_window, measure


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=ROOT / "bin/Debug-windows-x86_64/RedflagForge/RedflagForge.exe")
    parser.add_argument("--renderdoc", type=Path, default=ROOT / "tmp/tools/renderdoc/RenderDoc_1.47_64")
    parser.add_argument("--label", default="baseline")
    parser.add_argument("--same-data", type=Path, help="另一轮截帧的工作目录，复用其场景、程序集及 UI 素材")
    parser.add_argument("--window", nargs=2, type=int, metavar=("WIDTH","HEIGHT"))
    parser.add_argument("--replay", action="store_true")
    parser.add_argument("--timing", action="store_true", help="一次预热，然后测量每个绘制事件十一遍")
    parser.add_argument("--compare", type=Path, help="修改前同一画幅的截帧工作目录")
    args = parser.parse_args()
    exe = args.exe.resolve(strict=True)
    version = hashlib.sha256(exe.read_bytes()).hexdigest()
    renderdoc = args.renderdoc.resolve(strict=True)
    base = ROOT / "tmp/hud-coverage" / (args.label + "-" + version[:12])
    ui = ROOT / "RedflagForge/assets/ui"
    source = ROOT / "RedflagForge/assets/scenes/Watt-Station.redflag"
    for mode in ("hud", "empty"):
        work = base / mode
        if not args.replay:
            work.mkdir(parents=True, exist_ok=True)
            previous = args.same_data.resolve() / mode if args.same_data else None
            if not (work / "assets/ui").exists():
                shutil.copytree(previous / "assets/ui" if previous else ui, work / "assets/ui")
                shutil.copytree(previous / "Resources/Scripts" if previous else ROOT / "RedflagForge/Resources/Scripts", work / "Resources/Scripts",
                                ignore=shutil.ignore_patterns("Intermediates", "*.pdb"))
            prepare(work)
            input_scene = previous / "assets/scenes/Watt-Station.redflag" if previous else source
            data = yaml.safe_load(input_scene.read_text("utf8"))
            if mode == "empty":
                for e in data["Entities"]: e.pop("HudComponent", None)
            entry = work / "assets/scenes/Watt-Station.redflag"
            entry.write_text(yaml.safe_dump(data, sort_keys=False, allow_unicode=True), encoding="utf8")
            shutil.copy2(str(input_scene) + ".render.yaml", str(entry) + ".render.yaml")
            capture = ROOT / "output/captures/local" / base.name / mode / "game.rdc"
            capture.parent.mkdir(parents=True, exist_ok=True)
            env = dict(os.environ, PATH=str(ROOT / "bin/Debug-windows-x86_64/RedflagForge") + os.pathsep + os.environ["PATH"],
                       RF_STUDIO_WORK=str(work), RF_STUDIO_TOOLS=str(renderdoc), RF_STUDIO_EXE=str(exe), RF_STUDIO_CAPTURE=str(capture),
                       RF_STUDIO_GAME=str(entry), RF_STUDIO_FRAME="12")
            if args.window: env["RF_STUDIO_WINDOW"] = ",".join(map(str,args.window))
            run_renderdoc(renderdoc, ROOT / "tests/capture_material_studio.py", ROOT, env)
        capture = Path((work / "frame-path.txt").read_text("utf8").strip())
        env = dict(os.environ, RF_HUD_WORK=str(work), RF_HUD_CAPTURE=str(capture))
        if args.timing: env["RF_HUD_TIMING"] = "1"
        run_renderdoc(renderdoc, ROOT / "tests/replay_game_hud.py", ROOT, env)
        print(work / "hud.json")
    out = ROOT / "output/validation/watt-station/hud" / args.label
    summary = measure(base,out,version)
    if args.compare: compare_window(args.compare,base,out)
    print(summary["hud_draws"], summary["rasterized_hud_samples"], summary["pixels_by_hud_layers"])


if __name__ == "__main__":
    main()
