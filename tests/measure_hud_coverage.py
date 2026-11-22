"""读取 Frame Capture 导出的 ImGui 三角形。用像素中心和裁切矩形统计一帧内的重复覆盖。"""
import hashlib
import json
from pathlib import Path
import shutil
import struct
import numpy as np
from PIL import Image


def compare_window(before, after, out):
    def events(work):
        return json.loads((work / "empty/hud.json").read_text())["events"]
    original, changed = events(before), events(after)
    # 前两次绘制分别是窗口和游戏图。后者应继续使用相同顶点、UV 和裁切范围。
    assert len(original) == len(changed) == 2
    a, b = original[1], changed[1]
    for key in ("indices","stride","scissor","texture_size","push","Samples Passed"):
        assert a[key] == b[key], key
    vertices_equal = (before / f"empty/vertices-{a['event']}.bin").read_bytes() == (after / f"empty/vertices-{b['event']}.bin").read_bytes()
    assert vertices_equal
    old_pixels, new_pixels = (np.asarray(Image.open(work / "empty/game.png")) for work in (before,after))
    height, width = old_pixels.shape[:2]
    bg = [event[0]["Samples Passed"] for event in (original,changed)]
    check = dict(window_samples=bg,removed_samples=bg[0]-bg[1],scene_samples=a["Samples Passed"],same_scene_vertices_uv_scissor=True)
    tw, th = a["texture_size"]
    band = (height - round(width * th / tw)) // 2
    if band > 2:
        # 不把游戏画面或主题最外侧的细边误算作黑边。
        mask = np.zeros((height,width),bool)
        mask[2:band-1,2:-2] = mask[height-band+1:height-2,2:-2] = True
        difference = np.any(old_pixels[mask] != new_pixels[mask],axis=1)
        check.update(compared_bar_pixels=int(mask.sum()),different_bar_pixels=int(np.count_nonzero(difference)))
        assert not np.any(difference) and bg[0] == bg[1]
    elif width*th == height*tw:
        assert bg[0] - bg[1] == width*height
    (out / "background-check.json").write_text(json.dumps(check,indent=2)+"\n")


def measure(base, out, version):
    hud = base / "hud"
    info = json.loads((hud / "hud.json").read_text())
    empty = json.loads((base / "empty/hud.json").read_text())
    width, height = info["size"]
    background = len(empty["events"])
    assert [(e["indices"], e["texture_size"]) for e in info["events"][:background]] == [
        (e["indices"], e["texture_size"]) for e in empty["events"]]
    visits = np.zeros((height,width),np.uint16)
    blank = 0
    resources = {}
    rows = []
    vertex_type = np.dtype([("xy","<f4",(2,)),("uv","<f4",(2,)),("color","u1",(4,))])

    for draw in info["events"][background:]:
        eid = draw["event"]
        assert draw["stride"] == vertex_type.itemsize
        verts = np.fromfile(hud / f"vertices-{eid}.bin", dtype=vertex_type)
        indices = np.fromfile(hud / f"indices-{eid}.bin",np.uint16 if draw["index_size"] == 2 else np.uint32).astype(int)
        xy = verts["xy"].astype(np.float64)
        sx,sy,tx,ty = struct.unpack("<4f", bytes.fromhex(draw["push"]))
        xy = (xy * [sx,sy] + [tx+1,ty+1]) * [width/2,height/2]
        tw,th = draw["texture_size"]
        texture = draw["texture"]
        if texture not in resources:
            resources[texture] = np.fromfile(hud / draw["alpha"], np.uint8).reshape(th,tw)
        alpha = resources[texture]
        fragment_count = transparent = 0
        scx,scy,scw,sch = draw["scissor"]

        for tri in (indices - draw["first_index"]).reshape(-1,3):
            points, uv = xy[tri], verts["uv"][tri].astype(float)
            a,b,c = points
            area = (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])
            if area == 0: continue
            if area < 0:
                points, uv = points[[0,2,1]], uv[[0,2,1]]
                area = -area
            x0,y0 = np.maximum(np.floor(points.min(0)).astype(int),[max(0,scx),max(0,scy)])
            x1,y1 = np.minimum(np.ceil(points.max(0)).astype(int),[min(width,scx+scw),min(height,scy+sch)])
            if x0 >= x1 or y0 >= y1: continue
            y,x = np.ogrid[y0:y1,x0:x1]
            inside = np.ones((y1-y0,x1-x0),bool)
            weights = []
            for p,q in ((points[1],points[2]),(points[2],points[0]),(points[0],points[1])):
                dx,dy = q-p
                edge = dx*(y+.5-p[1])-dy*(x+.5-p[0])
                inside &= (edge>0) | ((edge==0) & (dy<0 or (dy==0 and dx>0)))
                weights.append(edge/area)
            visits[y0:y1,x0:x1] += inside
            fragment_count += int(inside.sum())
            # Vulkan 线性采样读取相邻四个透明度值。半像素偏移把归一化 UV 移到纹素中心。
            u,v = (np.stack(weights,-1)[inside] @ uv * [tw,th] - .5).T
            ix,iy = np.floor(u).astype(int), np.floor(v).astype(int)
            fx,fy = u-ix,v-iy
            sampled = np.zeros(len(u))
            for ox,oy,factor in ((0,0,(1-fx)*(1-fy)),(1,0,fx*(1-fy)),(0,1,(1-fx)*fy),(1,1,fx*fy)):
                sampled += alpha[np.clip(iy+oy,0,th-1),np.clip(ix+ox,0,tw-1)]*factor
            transparent += int(np.sum(sampled==0))
        blank += transparent
        rows.append(dict(event=eid,texture_size=draw["texture_size"],triangles=len(indices)//3,coverage=fragment_count,
                         fully_transparent_samples=transparent,gpu_samples=draw.get("Samples Passed"),
                         fragment_invocations=draw.get("PS Invocations")))

    out.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(hud / "game.png", out / "game.png")
    bins = np.bincount(visits.ravel())
    heat = np.array([[20,24,29],[93,156,168],[233,165,70],[213,79,73]],np.uint8)
    Image.fromarray(heat[np.minimum(visits,3)]).save(out / "coverage.png")
    files = {}
    for mode in ("hud","empty"):
        frame = Path((base / mode / "frame-path.txt").read_text().strip())
        with frame.open("rb") as stream: digest = hashlib.file_digest(stream,"sha256").hexdigest()
        files[mode] = dict(path=frame.relative_to(Path.cwd()).as_posix(),sha256=digest)
    inputs = ["assets/scenes/Watt-Station.redflag","assets/scenes/Watt-Station.redflag.render.yaml",
              "assets/ui/station/hud.yaml","Resources/Scripts/Sandbox.dll","Resources/Scripts/Redflag-ScriptCore.dll"]
    summary = dict(frame=info["frame"],renderdoc=info["renderdoc"],size=info["size"],captures=files,
        exe_sha256=version,inputs={p:hashlib.sha256((hud / p).read_bytes()).hexdigest() for p in inputs},
        game_draws=len(info["events"]),no_hud_draws=background,hud_draws=len(rows),hud_events=rows,
        window_and_scene=[dict(event=e["event"],triangles=e["indices"]//3,samples=e.get("Samples Passed")) for e in empty["events"]],
        pixels_by_hud_layers=bins.tolist(),rasterized_hud_samples=int(visits.sum()),zero_alpha_samples=blank,
        messages=info["messages"])
    if "duration_ms" in info["events"][0]:
        timings = np.array([event["duration_ms"] for event in info["events"]])
        summary["gpu_ms_11_replays"] = dict(window=timings[0].tolist(), scene=timings[1].tolist(),
                                           hud=timings[background:].sum(axis=0).tolist(),
                                           window_scene_hud=timings.sum(axis=0).tolist())
    (out / "summary.json").write_text(json.dumps(summary,indent=2)+"\n")
    return summary
