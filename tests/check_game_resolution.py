"""检查实机导出的 Game 目标、相机比例与面板显示尺寸。默认读取仓库里的两后端记录。"""
import csv
from pathlib import Path


root = Path(__file__).resolve().parents[1] / "output/validation/game-resolution"
for backend in ("vulkan", "opengl"):
    with (root / f"{backend}.csv").open(encoding="utf-8-sig") as source:
        rows = [{name: float(value) for name, value in row.items()} for row in csv.DictReader(source)]
    game = [r for r in rows if r["game"]]
    assert {(r["target_w"], r["target_h"]) for r in game} == {(1920, 1080), (1280, 720)}
    assert len({r["window_w"] for r in game if r["target_w"] == 1920}) == 2
    assert all(abs(r["projection_aspect"] - 16/9) < .0001 for r in game)
    displayed = [r for r in game if r["draw_h"]]
    assert displayed and all(abs(r["draw_w"]/r["draw_h"] - 16/9) < .0001 for r in displayed)
    assert rows[-1]["game"] == 0 and rows[-1]["target_w"] != 1280
    if backend == "vulkan":
        assert all((r["lighting_w"], r["lighting_h"]) == (r["target_w"], r["target_h"]) for r in game)
        assert rows[-1]["lighting_w"] == 320
    print(f"{backend}: 1080p, resize, 720p, Scene")

assert "Framebuffer Size: 1920x1080" in (root / "sandbox.txt").read_text(encoding="utf-8-sig")
print("Sandbox: 1920x1080")
