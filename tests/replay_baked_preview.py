"""在 RenderDoc 中读出第 24 帧的面板纹理；用系统 Python 对比两次导出。"""
import hashlib
import json
import os
from pathlib import Path
import sys


def actions(roots):
    for action in roots:
        yield action
        yield from actions(action.children)


def export(frame, work):
    import renderdoc as rd

    textures = {t.resourceId: t for t in frame.GetTextures()}
    dispatches, scene_draws, ui_draws = [], [], []
    panel = None
    for action in actions(frame.GetRootActions()):
        if action.flags & rd.ActionFlags.Dispatch:
            dispatches.append(action.eventId)
        if not action.flags & rd.ActionFlags.Drawcall:
            continue
        frame.SetFrameEvent(action.eventId, True)
        state = frame.GetPipelineState()
        outputs = [textures[d.resource] for d in state.GetOutputTargets() if d.resource in textures]
        if not any(t.creationFlags & rd.TextureCategory.SwapBuffer for t in outputs):
            scene_draws.append(action.eventId)
            continue
        ui_draws.append(action.eventId)
        # 光照可能在更早的帧就算好了。直接读取 ImGui 这次绘制场景面板时使用的纹理。
        for read in state.GetReadOnlyResources(rd.ShaderStage.Fragment):
            texture = textures.get(read.descriptor.resource)
            if texture and texture.width == 160 and texture.format.Name() == "R32G32B32A32_FLOAT":
                raw = frame.GetTextureData(texture.resourceId, rd.Subresource())
                (work / "panel.f32").write_bytes(raw)
                panel = dict(event=action.eventId, resource=str(texture.resourceId),
                             width=texture.width, height=texture.height, format=texture.format.Name(),
                             sha256=hashlib.sha256(raw).hexdigest())
    if not panel:
        raise ValueError(f"No 160-pixel lighting texture sampled in {len(ui_draws)} swapchain draws")
    info = frame.GetFrameInfo()
    return dict(frame=info.frameNumber, renderdoc=rd.GetVersionString(), panel=panel, dispatches=dispatches,
                scene_draws=scene_draws, ui_draws=ui_draws, messages=[m.description for m in info.debugMessages])


def replay():
    import renderdoc as rd
    import traceback

    path = Path(os.environ["RF_STUDIO_CAPTURE"])
    work = Path(os.environ["RF_STUDIO_WORK"])
    capture = rd.OpenCaptureFile()
    frame = None
    try:
        assert capture.OpenFile(str(path), "", None) == rd.ResultCode.Succeeded
        status, frame = capture.OpenCapture(rd.ReplayOptions(), None)
        assert status == rd.ResultCode.Succeeded
        result = export(frame, work)
        (work / "replay.json").write_text(json.dumps(result, indent=2), encoding="utf8")
    except BaseException:
        (work / "error.txt").write_text(traceback.format_exc(), encoding="utf8")
        sys.exit(1)
    finally:
        if frame:
            frame.Shutdown()
        capture.Shutdown()


def compare():
    import argparse
    from collections import Counter
    import numpy as np
    from PIL import Image
    import re
    import yaml

    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before", type=Path, help="Original editor executable")
    parser.add_argument("after", type=Path, help="Updated editor executable")
    args = parser.parse_args()
    out = root / "output/profiling/navigation-light/baked"
    out.mkdir(parents=True, exist_ok=True)
    scenes = root / "RedflagForge/assets/scenes"
    report = dict(scene="RTXGI-Bathroom.redflag", api="Vulkan", frame=24)
    images = []
    for label, exe in (("before", args.before), ("after", args.after)):
        with exe.open("rb") as f:
            digest = hashlib.file_digest(f, "sha256").hexdigest()
        work = root / "tmp/material-reference" / f"bathroom-{digest[:12]}-160-baked"
        path = Path((work / "frame-path.txt").read_text(encoding="utf8"))
        rec = json.loads((work / "replay.json").read_text(encoding="utf8"))
        rec.update(executable_sha256=digest, capture=path.relative_to(root).as_posix())
        log = (work / "editor.log").read_text(encoding="utf8", errors="replace")
        rec["gpu"] = re.search(r"Vulkan device: ([^\r\n]+)", log)[1]
        rec["viewport"] = list(map(int, re.findall(r"Lighting preview 160 x \d+, viewport (\d+) x (\d+)", log)[-1]))
        trace = json.loads((work / "RedflagProfile-Runtime.json").read_bytes())
        names = [e.get("name") for e in trace["traceEvents"] if e.get("name", "").startswith("Lighting ")]
        last_move = max(i for i, name in enumerate(names) if name == "Lighting camera motion")
        rec["trace_after_last_resize"] = dict(Counter(names[last_move:]))
        with path.open("rb") as f:
            rec["capture_sha256"] = hashlib.file_digest(f, "sha256").hexdigest()
        rec["capture_bytes"] = path.stat().st_size
        size = rec["panel"]
        image = np.fromfile(work / "panel.f32", "<f4").reshape(size["height"], size["width"], 4)
        rgb = image[:, :, :3]
        assert np.isfinite(image).all() and rgb.max() > rgb.min()
        Image.fromarray(np.rint(255 * np.clip(rgb, 0, 1)).astype("uint8")).save(out / (label + ".png"))
        images.append(image)
        report[label] = rec
        settings = yaml.safe_load((work / "assets/scenes/Sponza-Atrium.redflag.render.yaml").read_text(encoding="utf8"))
        assert settings == report.setdefault("settings", settings)
        scene = work / "assets/scenes/Sponza-Atrium.redflag"
        for suffix in ("", ".rfprobes"):
            original = (scenes / ("RTXGI-Bathroom.redflag" + suffix)).read_bytes()
            assert original == Path(str(scene) + suffix).read_bytes()
            report["probes_sha256" if suffix else "scene_sha256"] = hashlib.sha256(original).hexdigest()
    diff = abs(images[1].astype("float64") - images[0])
    report["comparison"] = dict(pixels=diff.shape[0] * diff.shape[1], changed_pixels=int(np.any(diff != 0, axis=2).sum()),
                                max_abs_rgba=float(diff.max()), mean_abs_rgb=float(diff[:, :, :3].mean()))
    (out / "frame24.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf8")
    print(json.dumps(report["comparison"]))
    before, after = report["before"], report["after"]
    assert before["dispatches"] and not after["dispatches"]
    assert len(before["scene_draws"]) == len(after["scene_draws"]) > 0
    assert len(before["ui_draws"]) == len(after["ui_draws"]) > 0
    assert before["panel"]["sha256"] == after["panel"]["sha256"], "Panel pixels changed"


if os.environ.get("RF_STUDIO_WORK"):
    replay()
    sys.exit(0)
if __name__ == "__main__":
    compare()
