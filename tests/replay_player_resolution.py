"""qrenderdoc --python 中回读游戏表面的 Vulkan 颜色附件和最终 HUD 的输出尺寸。"""
import hashlib
import json
import os
from pathlib import Path
import sys

import renderdoc as rd


def leaves(actions):
    for item in actions:
        yield item
        yield from leaves(item.children)


path = Path(os.environ["RF_GAME_RESOLUTION_RDC"])
cap = rd.OpenCaptureFile()
assert cap.OpenFile(str(path),"",None)==rd.ResultCode.Succeeded
status,frame = cap.OpenCapture(rd.ReplayOptions(),None)
assert status==rd.ResultCode.Succeeded
try:
    draws = [a for a in leaves(frame.GetRootActions()) if a.flags & rd.ActionFlags.Drawcall]
    resources = {t.resourceId:t for t in frame.GetTextures()}
    def targets(draw):
        frame.SetFrameEvent(draw.eventId,True)
        return [resources[t.resource] for t in frame.GetPipelineState().GetOutputTargets() if t.resource in resources]
    def describe(draw,textures):
        return dict(event=draw.eventId,indices=draw.numIndices,attachments=[{"size":[t.width,t.height],"format":t.format.Name()} for t in textures])
    for draw in draws:
        found = targets(draw)
        if len(found)>=2 and found[0].format.compType==rd.CompType.Float:
            surface=describe(draw,found)
            break
    hud = describe(draws[-1],targets(draws[-1]))
    with path.open("rb") as data:
        sha=hashlib.sha256()
        for chunk in iter(lambda:data.read(1<<20),b""): sha.update(chunk)
    result = dict(frame=frame.GetFrameInfo().frameNumber,renderdoc=rd.GetVersionString(),rdc_sha256=sha.hexdigest(),surface=surface,hud=hud)
    Path(os.environ["RF_GAME_RESOLUTION_JSON"]).write_text(json.dumps(result,indent=2)+"\n",encoding="utf8")
finally:
    frame.Shutdown()
    cap.Shutdown()
sys.exit(0)
