"""从地表带的米制路线生成普通 glTF，供 Forge 或命令行导入。"""
import argparse
import base64
import json
import math
from pathlib import Path
import struct
from tempfile import NamedTemporaryFile, gettempdir
from urllib.parse import quote


def curve(points, steps):
    if steps == 1 or len(points)<2:
        return points
    if not 1 <= steps <= 64:
        raise ValueError("相邻两点之间的分段数应在 1 到 64 之间")
    # Catmull–Rom 曲线经过每个编辑点。首尾向外延续切线，避免在端点缩短曲线。
    extended = [[2*a-b for a,b in zip(points[0],points[1])],*points,
                [2*a-b for a,b in zip(points[-1],points[-2])]]
    result=[]
    for i in range(len(points)-1):
        for step in range(steps):
            t=step/steps
            result.append([.5*(2*b + (-a+c)*t + (2*a-5*b+4*c-d)*t*t
                                + (-a+3*b-3*c+d)*t*t*t) for a,b,c,d in zip(*extended[i:i+4])])
    return result+[points[-1]]


def mesh(points, bands, repeat):
    lengths, directions = [0.0], []
    for a, b in zip(points, points[1:]):
        x, z = b[0]-a[0], b[1]-a[1]
        length = math.hypot(x,z)
        if length == 0:
            raise ValueError("路线中相邻两点重合")
        directions.append((x/length,z/length))
        lengths.append(lengths[-1]+length)
    if not directions or not bands or repeat < 0:
        raise ValueError("地表带需要至少两个路径点、一条有宽度的带和非负重复长度")

    # 在转角处用两侧切线的角平分线外扩。左右两段在同一个截面相接。
    right = []
    for i in range(len(points)):
        a,b = directions[max(0,i-1)], directions[min(i,len(directions)-1)]
        scale = 1 + a[0]*b[0] + a[1]*b[1]
        if scale < 1e-5:
            raise ValueError("路线不能在同一点掉头；请给弯道增加路径点")
        right.append((-(a[1]+b[1])/scale,(a[0]+b[0])/scale))
    pos, uv, indices = [], [], []
    for band in bands:
        width, offset = band["width"], band.get("offset",0)
        if width <= 0:
            raise ValueError("地表带宽度须大于零米")
        lo,hi = band.get("u",[0,1])
        start = len(pos)
        for i,((x,z),(rx,rz)) in enumerate(zip(points,right)):
            for across, u in ((offset-width/2,lo),(offset+width/2,hi)):
                pos.append((x+rx*across,0,z+rz*across))
                uv.append((u,lengths[i]/(repeat or lengths[-1])))
            if i+1 < len(points):
                a = start + i*2
                indices.extend((a,a+1,a+2, a+2,a+1,a+3))
    return pos,uv,indices


def bake(profile):
    source = json.loads(profile.read_text(encoding="utf8"))
    points = curve(source["points"],source.get("subdivisions",1))
    pos,uv,indices = mesh(points,source["bands"],source.get("repeat_m",0))
    blocks = [b"".join(struct.pack("<3f",*p) for p in pos),struct.pack("<3f",0,1,0)*len(pos),
              b"".join(struct.pack("<2f",*p) for p in uv),struct.pack(f"<{len(indices)}I",*indices)]
    blob = b"".join(blocks)
    access = [dict(bufferView=i,componentType=5125 if i==3 else 5126,count=n,type=t)
              for i,(n,t) in enumerate(((len(pos),"VEC3"),(len(pos),"VEC3"),(len(uv),"VEC2"),(len(indices),"SCALAR")))]
    access[0].update(min=list(map(min,zip(*pos))),max=list(map(max,zip(*pos))))
    images = []
    pbr = dict(metallicFactor=source.get("metallic",0),roughnessFactor=source.get("roughness",1))
    material = dict(name=profile.stem,pbrMetallicRoughness=pbr)
    if "alpha_cutoff" in source:
        material.update(alphaMode="MASK",alphaCutoff=source["alpha_cutoff"])
    for key, parent, field in (("color",pbr,"baseColorTexture"),("normal",material,"normalTexture"),
                               ("metallic_roughness",pbr,"metallicRoughnessTexture")):
        if key not in source: continue
        filename=source[key]
        if Path(filename).is_absolute() or not (profile.parent/filename).is_file():
            raise ValueError("贴图需要使用存在的相对文件名 "+filename)
        parent[field]={"index":len(images)}
        images.append({"uri":quote(Path(filename).as_posix())})
    model = dict(asset={"version":"2.0","generator":"Redflag Surface Strip"},scene=0,scenes=[{"nodes":[0]}],
                 nodes=[{"mesh":0}],meshes=[{"primitives":[dict(attributes={"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},
                                                               indices=3,material=0)]}],materials=[material],
                 buffers=[{"byteLength":len(blob),"uri":"data:application/octet-stream;base64,"+base64.b64encode(blob).decode()}],
                 bufferViews=[dict(buffer=0,byteOffset=sum(map(len,blocks[:i])),byteLength=len(block))
                              for i,block in enumerate(blocks)],accessors=access)
    if images:
        model.update(images=images,textures=[{"source":i} for i in range(len(images))])
    output = profile.with_suffix(".gltf")
    if output.exists() and json.loads(output.read_bytes()).get("asset",{}).get("generator") != model["asset"]["generator"]:
        raise ValueError("同名 glTF 由其他工具创建。请更改地表带文件名以保留原模型。")
    with NamedTemporaryFile(dir=output.parent,delete=False) as tmp:
        tmp.write((json.dumps(model,ensure_ascii=False,indent=2)+"\n").encode())
    try:
        Path(tmp.name).replace(output)
    finally:
        Path(tmp.name).unlink(missing_ok=True)
    return output


if __name__ == "__main__":
    cli=argparse.ArgumentParser(description=__doc__)
    cli.add_argument("file",type=Path,help=".rfstrip 或 Forge 的临时请求文件")
    path=cli.parse_args().file
    if path.suffix.lower()==".rfstrip":
        print(bake(path))
    elif path.suffix.lower()==".tmp" and path.resolve().parent==Path(gettempdir()).resolve():
        from ruamel.yaml import YAML
        try:
            request=YAML(typ="safe").load(path.read_bytes())
            result={"model":str(bake(Path(request["profile"])))}
        except (KeyError, OSError, TypeError, ValueError) as exc:
            result={"error":str(exc)}
        path.write_text(json.dumps(result,ensure_ascii=False),encoding="utf8")
    else:
        cli.error("请选择 .rfstrip 地表带文件")
