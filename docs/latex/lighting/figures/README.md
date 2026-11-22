# 光照册图源

## Stanford 兔子与龙

DDGI 章中的三张兔子图使用 `Reference-StanfordBunny.redflag` 的同一机位。左图是 Vulkan 逐灯直接光，中图是探针插值的一次反弹，第三张先在线性 RGB 中将前两者相加，再做与基准一致的曝光和显示转换。三张图都保留原始画幅，每张 288 × 192 像素。探针每方向 16 条射线、更新 8 帧。[场景与实测设置](../../../../output/scenes/stanford-vulkan/metrics.json) · [基准调用](../../../../tests/StanfordEffects.cpp)

GTAO 章用同一批测试中龙的直接光与遮挡诊断输出。右图用可见度直接映射为 0 至 255 的灰度，蓝灰底色标出没有命中场景表面的像素。照明图和可见度图各自使用适合该数据的显示方式。[可见度计算](../../../../Redflag/src/Redflag/Renderer/ScreenSpace/Shaders/GTAO.comp) · [Stanford 模型出处](../../../../output/scenes/Stanford-Scans.md)

这些 PNG 随本册保存。在仓库根目录运行 `tests\run_stanford_effects.cmd` 可重新生成浮点结果，再运行 `python docs/latex/lighting/figures/stanford_views.py` 导出书中的五张图。反射点和遮挡物的侧面图是本册的 TikZ 原图，见 [ReSTIR GI 一章](../restir-gi.tex)。

## 浴室烘焙按钮

`probe-controls.png` 是正式 Vulkan 编辑器中浴室尚在实时更新时的设置。`probe-reopened.png` 截自另一个编辑器进程的第 12 帧，先前的场景已开启多次漫反射，在完成四轮烘焙后按 Ctrl+S 再次保存。两张原图均为 2560 × 1440，裁取左上角坐标 (1853, 920) 起的 310 × 125 像素，画面中的勾选和按钮都是实际状态。[帧文件、哈希和计算事件](../../../../output/profiling/probe-bake/bathroom-reopen.json)

## 棱镜示意图

Suidroot 于 2008 年发表 *Prism-rainbow.svg*。本册选用原作者提供的 CC BY-SA 4.0 许可。目录内的 SVG 是原件，PDF 只转换矢量格式，图形与色彩保持原样。[作者和发布记录](https://commons.wikimedia.org/wiki/File:Prism-rainbow.svg) · [SVG 原件](https://upload.wikimedia.org/wikipedia/commons/2/24/Prism-rainbow.svg) · [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)

安装 Playwright 后，在装有 Microsoft Edge 的机器上进入本目录，运行下列 Python 代码，可由 SVG 生成保留原图渐变和滤镜的 PDF。

```python
from pathlib import Path
from playwright.sync_api import sync_playwright

source = Path("prism-rainbow.svg")
svg = source.read_text(encoding="utf8")
style = "<style>@page{size:400px 300px;margin:0}body{margin:0}svg{display:block}</style>"
with sync_playwright() as browser_tools:
    browser = browser_tools.chromium.launch(channel="msedge")
    page = browser.new_page(viewport={"width": 400, "height": 300})
    page.set_content(style + svg[svg.index("<svg"):])
    page.pdf(path=str(source.with_suffix(".pdf")), prefer_css_page_size=True, print_background=True)
    browser.close()
```
