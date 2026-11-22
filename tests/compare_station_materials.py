"""Compare only visible pixels of the chosen surface in two recorded Watt-Station frames."""
import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT/"output/validation/station-materials"


def surface(work, name, size):
    raw = work/name
    h,w = size[1],size[0]
    def values(part,channels):
        return np.fromfile(raw/(part+".f32"),np.float32).reshape(h,w,channels)
    before,after,final = (values(part+"-depth",1)[:,:,0] for part in ("before","after","final"))
    # 绘制改变了深度，且后续绘制仍保留这份深度，说明此处最终可见。
    mask = (after!=before)&(after==final)
    return mask,values("normal",4),values("albedo",4),np.asarray(Image.open(OUT/(name+".png")).convert("RGB"))


def compare(work, first, second, crop=None):
    a,b = [json.loads((OUT/(name+".json")).read_text(encoding="utf8")) for name in (first,second)]
    w,h = a["size"]
    old,n0,c0,img0 = surface(work,first,(w,h))
    new,n1,c1,img1 = surface(work,second,(w,h))
    both = old & new
    if not both.any(): raise ValueError("Selected surface is invisible at "+first)
    visible_bounds = Image.fromarray(both).getbbox()
    if crop is None: crop=visible_bounds
    u,v,r,t = crop
    delta = np.abs(img1.astype(np.float32)-img0)
    planes = [Image.fromarray(image[v:t,u:r]) for image in (img0,img1)]
    comparison = Image.new("RGB",(planes[0].width*2,planes[0].height))
    comparison.paste(planes[0],(0,0))
    comparison.paste(planes[1],(planes[0].width,0))
    name = first+"-"+second.split("-")[-1]
    comparison.save(OUT/(name+"-crop.png"))
    mat0,mat1 = (np.array(d["material_draws"][0]["clip_matrix"]).reshape(4,4) for d in (a,b))
    # 展台原来只有地面法向的缩放不同。平面所有顶点局部 Z 都是 0。
    axes = [0,1,3] if name.startswith(("spawn","gravel")) else [0,1,2,3]
    normalized0,normalized1 = n0[:,:,:3][both],n1[:,:,:3][both]
    cosine = np.sum(normalized0*normalized1,axis=1)/np.maximum(1e-12,
                  np.linalg.norm(normalized0,axis=1)*np.linalg.norm(normalized1,axis=1))
    degrees=np.degrees(np.arccos(np.clip(cosine,-1,1)))
    result=dict(view=name, old_surface=int(old.sum()),new_surface=int(new.sum()),common_pixels=int(both.sum()),
                changed_visible_pixels=int((old^new).sum()), final_rgb_mae=float(delta[both].mean()),
                normal_angle_degrees_median=float(np.median(degrees)),normal_angle_degrees_p95=float(np.percentile(degrees,95)),
                changed_albedo_pixels=int((np.linalg.norm(c0[:,:,:3][both]-c1[:,:,:3][both],axis=1)>.002).sum()),
                clip_matrix_max_difference=float(np.max(np.abs(mat0[axes]-mat1[axes]))),
                crop=list(map(int,crop)),bounds=list(map(int,visible_bounds)))
    if name.startswith("rim"):
        p,q=(np.fromfile(work/label/"clip.f32",np.float32).reshape(-1,4).astype(np.float64)
             for label in (first,second))
        pixels=np.linalg.norm((p[:,:2]/p[:,3:4]-q[:,:2]/q[:,3:4])*[w/2,h/2],axis=1)
        # 顶点着色器已将 Z 从 OpenGL 范围换成 Vulkan 范围，反求模型位置时先还原。
        p[:,2]=2*p[:,2]-p[:,3]
        q[:,2]=2*q[:,2]-q[:,3]
        displacement=np.linalg.norm(((p-q)@np.linalg.inv(mat1))[:,:3],axis=1)*1000
        result.update(indexed_vertices=len(p),pixel_shift_p50_p95_max=np.percentile(pixels,[50,95,100]).tolist(),
                      displacement_mm_p50_p95_max=np.percentile(displacement,[50,95,100]).tolist())
    print(name,result)
    return result


if __name__=="__main__":
    cli=argparse.ArgumentParser(description=__doc__)
    cli.add_argument("work",type=Path)
    a=cli.parse_args()
    result = [
        compare(a.work,"spawn-original","spawn-normal",(990,900,1410,1070)),
        compare(a.work,"spawn-normal","spawn-current",(990,900,1410,1070)),
        compare(a.work,"gravel-normal","gravel-current",(910,810,1450,1050)),
        compare(a.work,"rack-original","rack-current"),
        compare(a.work,"rim-original","rim-current")]
    (OUT/"comparison.json").write_text(json.dumps(result,indent=2)+"\n",encoding="utf8")
