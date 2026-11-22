"""用 RenderDoc 导出的浮点样点复算面板；图中颜色直接取自最终显示派发的输出。"""
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont


def tone(light):
    light=np.maximum(light,np.float32(0))
    return np.power(light/(1+light),np.float32(1/2.2))


def verify(work,out,mode,replay):
    p=replay["panel"]
    if "device" in replay and replay["device"]=="Vulkan":
        replay["api"]=replay.pop("device")
    w,h=p["size"]
    side=p["sample_side"]
    assert side=={"off":1,"on":2}[mode] and replay["frame"]==24 and not replay["messages"]
    linear=np.fromfile(work/"ssaa-light.f32","<f4").reshape(h,side,w,side,4)[...,:3]
    display=np.fromfile(work/"ssaa-panel.f32","<f4").reshape(h,w,4)
    assert np.isfinite(linear).all() and np.isfinite(display).all() and np.all(display[:,:,3]==1)
    if side==1:
        average=linear[:,0,:,0]
    else:
        average=(linear[:,0,:,0]+linear[:,0,:,1]+linear[:,1,:,0]+linear[:,1,:,1])*.25
    expected=tone(average)
    error=np.abs(display[:,:,:3]-expected)
    assert float(error.max())<.000002
    wrong=np.abs(tone(linear).mean(axis=(1,3))-display[:,:,:3]).max(axis=2)
    y,x=map(int,np.unravel_index(wrong.argmax(),wrong.shape))
    row=dict(checked_pixels=w*h,checked_rgb_values=w*h*3,linear_samples=w*h*side**2,
             max_abs_rgb_error=float(error.max()),mean_abs_rgb_error=float(error.mean()),
             wrong_order_max_error=float(wrong.max()),wrong_order_pixels_over_one_8bit_level=int((wrong>1/255).sum()),
             example_xy=[x,y],linear_rgb=linear[y,:,x].reshape(-1,3).tolist(),average_rgb=average[y,x].tolist(),
             captured_display_rgb=display[y,x,:3].tolist(),expected_rgb=expected[y,x].tolist())
    replay["verification"]=row
    (out/(mode+"-replay.json")).write_text(json.dumps(replay,indent=2)+"\n",encoding="utf8")
    Image.fromarray(np.uint8(np.clip(display[:,:,:3],0,1)*255+.5)).save(out/(mode+".png"))


def comparison(out):
    infos=[json.loads((out/(mode+"-replay.json")).read_bytes()) for mode in ("off","on")]
    old,new=[r["panel"] for r in infos]
    assert old["size"]==new["size"] and new["source_bytes"]==4*old["source_bytes"]
    captures=[json.loads((out/(mode+"-capture.json")).read_bytes()) for mode in ("off","on")]
    assert captures[0]["scene_sha256"]==captures[1]["scene_sha256"]
    assert captures[0]["executable_sha256"]==captures[1]["executable_sha256"]
    assert {k for k,v in captures[0]["settings"].items() if captures[1]["settings"][k]!=v}=={"Supersampling"}
    w,h=old["size"]
    pictures=[Image.open(out/(mode+".png")).convert("RGB") for mode in ("off","on")]
    canvas=Image.new("RGB",(w*2+8,h+28),"white")
    draw=ImageDraw.Draw(canvas)
    font=ImageFont.truetype("msyh.ttc",16)
    for i,(img,title) in enumerate(zip(pictures,("每像素一个着色样点","SSAA 2×2"))):
        canvas.paste(img,((w+8)*i,0))
        draw.text(((w+8)*i+3,h+2),title,fill="#27323a",font=font)
    canvas.save(out/"sponza.png")
    # 固定镜头中的右侧树叶与石地。两边使用相同的像素位置和最近邻放大，方便观察边缘。
    crop=(222,173,300,239)
    detail=Image.new("RGB",(2*78*3+8,66*3+25),"white")
    label=ImageDraw.Draw(detail)
    for i,(img,title) in enumerate(zip(pictures,("SSAA 关闭","SSAA 2×2"))):
        detail.paste(img.crop(crop).resize((234,198),Image.Resampling.NEAREST),(242*i,0))
        label.text((242*i+3,200),title,font=font,fill="#27323a")
    detail.save(out/"foliage.png")
