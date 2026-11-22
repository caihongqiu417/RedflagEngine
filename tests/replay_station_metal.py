"""用 RenderDoc 在维修站同一帧替换金属反射着色器，保留相机、灯光和 TAA 历史。"""

import hashlib
import json
import os
import re
import struct
import subprocess
import sys
import traceback
from pathlib import Path

import renderdoc as rd


def actions(nodes):
    for node in nodes:
        yield node
        yield from actions(node.children)


def bytecode(data):
    words = [int(v, 16) for v in re.findall(rb"0x[0-9a-fA-F]+", data)]
    return struct.pack(f"<{len(words)}I", *words)


work = Path(os.environ["RF_RACK_WORK"])
output = Path(os.environ["RF_RACK_OUT"])
file = Path((work / "frame-path.txt").read_text(encoding="utf8").strip())
base = "18a014d"
folder = Path("Redflag/src/Redflag/Renderer/ScreenSpace/Shaders")
current = {}
for name in (
    "StochasticSSR",
    "StochasticSceneSSR",
    "StochasticSceneSSRAlphaMask",
    "MultiReflection",
    "MultiReflectionAlphaMask",
):
    path = folder / (name + ".comp.inc")
    current[hashlib.sha256(bytecode(path.read_bytes())).digest()] = path

cap = rd.OpenCaptureFile()
ctrl = None
try:
    assert cap.OpenFile(str(file), "", None) == rd.ResultCode.Succeeded
    status, ctrl = cap.OpenCapture(rd.ReplayOptions(), None)
    assert status == rd.ResultCode.Succeeded
    calls = list(actions(ctrl.GetRootActions()))
    dispatches = [c.eventId for c in calls if c.flags & rd.ActionFlags.Dispatch]
    matches = []
    stage = rd.ShaderStage.Compute
    for event in dispatches:
        ctrl.SetFrameEvent(event, True)
        state = ctrl.GetPipelineState()
        shader = state.GetShaderReflection(stage)
        key = hashlib.sha256(bytes(shader.rawBytes)).digest()
        if key in current:
            slots = {
                shader.readWriteResources[r.access.index].fixedBindNumber: r.descriptor.resource
                for r in state.GetReadWriteResources(stage)
            }
            matches.append((event, shader.resourceId, current[key], slots))
    assert len(matches) == 1, f"Expected one production reflection dispatch; found {len(matches)}"
    event, original, path, slots = matches[0]
    old = bytecode(subprocess.check_output(["git", "show", base + ":" + path.as_posix()]))
    replacement, err = ctrl.BuildTargetShader(
        "main", rd.ShaderEncoding.SPIRV, old, rd.ShaderCompileFlags(), stage
    )
    assert replacement != rd.ResourceId.Null(), err

    # 在替换前后分别导出反射 pass 的输入与输出、曝光前的整帧光照，以及最后显示的游戏图像。
    width, height = 1920, 1080
    for label in ("current", "single"):
        if label == "single":
            ctrl.ReplaceResource(original, replacement)
        ctrl.SetFrameEvent(event, True)
        for slot, suffix in ((2, "input"), (3, "reflected")):
            data = bytes(ctrl.GetBufferData(slots[slot], 0, width * height * 16))
            (work / ("replay-" + label + "-" + suffix + ".f32")).write_bytes(data)
        ctrl.SetFrameEvent(dispatches[-1], True)
        resources = [r.descriptor.resource for r in ctrl.GetPipelineState().GetReadWriteResources(stage)]
        ids = {t.resourceId for t in ctrl.GetTextures()}
        (buffer,) = [r for r in resources if r not in ids]
        data = bytes(ctrl.GetBufferData(buffer, 0, width * height * 16))
        (work / ("replay-" + label + "-light.f32")).write_bytes(data)
        ctrl.SetFrameEvent([a for a in calls if a.flags & rd.ActionFlags.Drawcall][-1].eventId, True)
        save = rd.TextureSave()
        save.resourceId = ctrl.GetPipelineState().GetOutputTargets()[0].resource
        save.destType = rd.FileType.PNG
        assert ctrl.SaveTexture(save, str(work / ("replay-" + label + ".png"))) == rd.ResultCode.Succeeded
    ctrl.RemoveReplacement(original)
    ctrl.FreeTargetResource(replacement)
    report = {
        "event": event,
        "shader": path.name,
        "baseline": base,
        "current_spirv_sha256": hashlib.sha256(bytecode(path.read_bytes())).hexdigest(),
        "old_spirv_sha256": hashlib.sha256(old).hexdigest(),
        "renderdoc": rd.GetVersionString(),
        "capture": file.relative_to(Path.cwd()).as_posix(),
    }
except Exception:
    report = {"error": traceback.format_exc()}
finally:
    if ctrl:
        ctrl.Shutdown()
    cap.Shutdown()
    (output / "same-frame.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf8")
sys.exit(int("error" in report))
