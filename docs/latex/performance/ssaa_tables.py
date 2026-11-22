"""从同一版编辑器的第 24 帧与独立进程计时生成 SSAA 表。"""
from pathlib import Path
import json

here=Path(__file__).resolve().parent
root=here.parents[2]
data=root/"output/profiling/ssaa"

def read(name):
    return json.loads((data/(name+".json")).read_bytes())

captures=[read(mode+"-capture") for mode in ("off","on")]
frames=[read(mode+"-replay") for mode in ("off","on")]
editor,gpu=read("editor"),read("gpu")
assert captures[0]["executable_sha256"]==captures[1]["executable_sha256"]==editor["executable_sha256"]==gpu["executable_sha256"]
assert captures[0]["scene_sha256"]==captures[1]["scene_sha256"]
assert {k for k in captures[0]["settings"] if captures[0]["settings"][k]!=captures[1]["settings"][k]}=={"Supersampling"}
assert all(f["frame"]==24 and not f["messages"] and f["verification"]["max_abs_rgb_error"]<=2e-6 for f in frames)

def table(name, rows):
    body="\n".join(f"{label} & {off} & {on}" + r"\\" for label,off,on in rows)
    return "\\newcommand{\\"+name+"}{\n"+body+"\n}\n"

def binary_bytes(value):
    return f"{value/1048576:.2f}"

def numbers(name,key):
    return [f"{r[name][key]:,}" for r in frames]

storage=[("每帧绘制 / 计算派发",*[f'{r["draws"]} / {r["dispatches"]}' for r in frames]),
         ("内部着色样点",*numbers("verification","linear_samples")),
         ("最终线性颜色缓冲区（MiB）",*[binary_bytes(r["panel"]["source_bytes"]) for r in frames]),
         ("截帧中全部缓冲区（MiB）",*[binary_bytes(r["storage"]["buffer_bytes"]) for r in frames]),
         ("截帧中全部图像（MiB）",*[binary_bytes(r["storage"]["image_bytes"]) for r in frames])]
times=[]
for title,key in [("只画光照表面，103 次绘制","scene_draws"),("全部 12 次计算派发","compute"),
                  ("其中显示转换一次","panel"),("全部绘制与计算派发之和","draws_and_compute")]:
    times.append((title,*[f'{gpu["cases"][m]["median_ms"][key]:.3f}' for m in ("off","on")]))
times.append(("提交队列调用合计，仅截帧当次",*[f'{sum(gpu["cases"][m]["queue_submit_cpu_us"])/1000:.3f}' for m in ("off","on")]))
for title,key in [("整帧重画，P50 / P95","frame_ms"),("沿用面板图，P50 / P95","cached_ms")]:
    vals=[editor["summary"][m][key] for m in ("off","on")]
    times.append((title,*[f'{v["p50_ms"]:.2f} / {v["p95_ms"]:.2f}' for v in vals]))
p=frames[1]["panel"]
v=frames[1]["verification"]
x,y=v["example_xy"]
red=[rgb[0] for rgb in v["linear_rgb"]]
settings=captures[1]["executable_sha256"][:12]
detail=(f"在面板像素 $({x},{y})$，四个位置的线性红色亮度分别为 {red[0]:.3f}、{red[1]:.3f}、"
        f"{red[2]:.3f} 和 {red[3]:.3f}。先求平均得到 {v['average_rgb'][0]:.3f}，再按 "
        f"$D(L)=(L/(1+L))^{{1/2.2}}$ 转为显示值，结果为 {v['captured_display_rgb'][0]:.3f}，和截帧一致。"
        f"若先把每个样点转为显示值再求平均，这个像素的 RGB 中偏差最大的通道相差 {v['wrong_order_max_error']:.3f}。")
verified=(f"在 RenderDoc 事件 {p['event']} 读回线性颜色缓冲区与面板图，对 {v['checked_pixels']:,} 个输出像素的 RGB 逐个复算。"
          f"最大绝对差小于 $1.2\\times10^{{-7}}$。若交换两步运算次序，"
          f"有 {v['wrong_order_pixels_over_one_8bit_level']:,} 个像素至少一个通道相差超过 $1/255$。")
assert v["max_abs_rgb_error"]<1.2e-7
body=table("ssaaStorage",storage)+table("ssaaTimes",times)
for key,value in (("ssaaExample",detail),("ssaaVerified",verified),("ssaaBuild",settings)):
    body+="\\newcommand{\\"+key+"}{"+value+"}\n"
(here/"build/ssaa-rows.tex").write_text(body,encoding="utf8")
