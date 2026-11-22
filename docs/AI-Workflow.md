# Redflag 的 AI 协作工作流

## 从教材例子到可复现的画面

先用教材里的小例子确定可独立检查的量。以 TAA 为例，GAMES101 的相机变换给出一个静态场景应满足的条件：相机没动，同一块石墙在两张未抖动的投影中位移为零。固定场景后用程序绘制连续帧，保存线性读回，用同一组子像素采样的均值检查细节，用相邻帧亮度差检查闪动。最后另抓实际编辑器窗口，核对显示尺寸和内部图像是否匹配。[画质回归](../tests/temporal_quality.py) · [回归数据](../output/validation/temporal/metrics.json)

每篇算法的可编辑笔记与材料索引放在一起。运行 `python docs/latex/lighting/build_notes.py` 会检查六段顺序和源码入口，读取真实实验的 JSON，生成章节索引并用 XeLaTeX 编译。改公开接口时也重新生成离线 API 手册，读者就能从游戏操作找到方法和对应源码。[教材与实现索引](latex/lighting/README.md) · [API 生成器说明](api/README.md)

核对渲染注释时，沿着变量读到实际使用它的那一行。比如组合光照先用 GTAO 减弱间接光，再加点光源的直接光。这样写，读者就能从代码判断调暗墙角会不会也调暗灯光。遇到试出来的数值，记下它量什么、单位是什么、加大后会多算什么。要验证“镜头没动时只画一帧”，就给同一个场景设好相机，数编辑器实际重画了几帧。[回归脚本](../tests/check_preview_frames.py)

## 做一次渲染优化

排查铁丝网、树叶这类镂空材质时，可以先把点光源放在相机的位置，让薄片后方的白墙接光。镜头能透过孔看到哪几个像素，灯也该照亮那几个像素。再在孔后加一块小挡板，就能查出光线穿过第一层后有没有继续判断遮挡。先在这个小场景核对拾取、阴影和倒影，再回到 Sponza 测实际开销。 [带孔薄片的 GPU 读数](../output/validation/gi-mask/after.json) · [RenderDoc 记录](../output/validation/gi-mask/replay.json)

同一张铁丝网要看两种距离。近看时让一根铁丝占几个屏幕像素，在源图、上传到显卡的透明度、相机画出的轮廓和墙上阴影里找同一根线。远看时记录一个屏幕像素覆盖的原图列数，再去 RenderDoc 看相机实际读取的缩小纹理层。两种距离曾经都看起来像“铁丝消失”，却分别是导入时漏掉了原图的列，以及远处的材质把整片网裁掉了。验证程序和库先分别保存校验值，等其他测试结束再独占显卡计时。[近景与远景的复现方法](../output/validation/fine-mask/README.md)

检查远处细线时，要试横纹、竖纹和斜纹，让镜头后退，也要横移。先按纹理在每个像素内的实际面积画出参考图，再比较每帧的误差以及相邻两帧变化了多少。曾有一种方法在竖纹上改善了两个读数，却让斜纹横移时的误差接近翻倍。还要试亮铁丝配暗背景、暗铁丝配亮背景，以及两片网互相遮挡。这样能分别查到算错亮度和算错遮挡的问题。[三种纹理的运动对照](../output/validation/mask-footprint-compare-rank/report.json) · [两层网的实测](../output/validation/mask-supersampling/subpixel-materials.csv)

查材质换色后反光是否正确，要把读数放在另一件物体上。把红墙换成蓝墙，保留灰地板、白色点光源和原来的机位。先比较每个探针保存的 RGB，再从 RenderDoc 的底色图中选出两次都属于地板的像素，检查这些位置在最终光照图中有没有换色。这样可以发现相机已经画对墙面、烘焙却还在读旧图的错误。[测试脚本](../tests/check_probe_materials.py) · [地板与探针的读数](../output/profiling/probe-materials/after.json)

检查反射降噪时，先看墙上的亮点有没有减少，再检查窗框和砖缝是否还在。用 RenderDoc 保存同一帧的相机、材质和灯，在测试里重画一次。每个像素都与截帧中的颜色一致后，只改变反射光线的方向，取 256 次结果的平均值作参照。比较的是 TAA 合并多帧之前的颜色。计时则单独运行编辑器，把真正重画场景的帧和只显示上一张图的帧分别统计。[浴室截帧与参考图](../output/profiling/rough-reflections/bathroom.json) · [整帧计时](../output/profiling/rough-reflections/editor-timing.json)

发现倒影闪动时，让相机、镜面和像素内的取样位置都保持不动。先单独读取连续 16 帧的倒影颜色，再去看开着 TAA 的最终画面。若光滑镜面上的同一点有时读到亮条、有时读到黑框，就能沿反射方向继续查。镜面工作室还在灯牌前放过一块挡板，并用 RenderDoc 确认显卡只在反射这一步改变了哪一块颜色。换到 RTXGI Bathroom 后，再量一次这一步的耗时。[固定像素的测试](../output/profiling/scene-reflections/test-method.md) · [编辑器截帧与计时](../output/profiling/scene-reflections/frames.json)

修模型接缝时，可以用一张薄墙区分两种错误。先把两片三角形拼紧，查出有多少光线穿过了本该封住的墙；再把它们真正分开，确认光还能穿过窄缝。随后用同一场景、同一机位比较旧版、修正后的第一版和最终版，分别记录查阴影、更新探针和算雾的时间。这样既能发现错误，也能看出修正花掉的时间落在哪一步。[求交回归](../tests/TriangleIntersectionCheck.h) · [两个经典场景的 GPU 记录](../output/profiling/triangle-intersection/README.md)

核对材质时，先将一个可手算的值送入正式编辑器。例如给两个三角形一张 2×2 材质图，在 RenderDoc 逐个读回粗糙度和金属度。再换成带多张真实贴图的古董相机，固定灯、相机、画幅和帧号，只切换镜面环境光。先核对两帧的底色与材质图完全相同，再比较同一小块机身边框的线性亮度。用于手册的 PNG 从编辑器最后一张图导出。这样，报告里的数字和读者看到的倒影可以在同一份原始帧中找到。[色板](../output/profiling/material-channels/README.md) · [摄影棚与复现命令](../output/profiling/material-studio/README.md)

这次数学册的卡片图重画后，我们先算出 60° 时截获的光减半，再用纯色环境图查实际结果。旧程序以每格的起点代表整格，在 $(2,1,0.5)$ 的场景里烘焙出的颜色略暗。改取每格中心，维持 15,876 次采样，在六个面的 6,144 个半精度像素上都读回原色。数学册构建会重算几何例子，并在去掉教学注释后核对每个源码摘录；GPU 测试另从纹理读回结果。这样能分别发现讲义推导错误、片段过期和运行时误差。[数学复算](latex/math/verify.py) · [GPU 用例](../tests/IBLTests.cpp)

从改动要节省的工作开始。2D 合批想减少重复提交，先固定同一组精灵，逐个画一遍，再整批画一遍。深度预写想少给被遮挡的表面着色，就要同时检查预写新增的工作和主绘制省下的工作。先读实际调用代码，再选能观察到这项差别的场景。

截帧时保留两种画法的输入，在 RenderDoc 找到对应事件，读出索引数、实例数和可用的硬件计数。对照同一张图的颜色、深度或拾取编号，确认画面的一致性。耗时另开进程采集。保留每一帧的 CPU 和 GPU 记录，LaTeX 性能册编译时从 CSV 生成统计表。原始帧、命令与本机条件随实验一起保存。[2D 精灵实验](../output/profiling/batch2d/README.md) · [3D 网格实验](../output/profiling/mesh/README.md) · [渲染性能实测](../output/pdf/Redflag-Rendering-Performance.pdf)

本次用 RenderDoc 检查 2D 合批时，事件面板里清空拾取图的调用出现了意外的整数。查看测试才发现只传入一个整数，而 OpenGL 要求传入四个分量。拾取图只存第一分量，所以最终画面对比仍然通过。修正后，重新采集了捕获、事件截图和计时数据。为避免以后换了帧却沿用旧报告，性能册在编译时先核对四份捕获的 SHA-256，再从每帧计时重新生成表格。[Khronos 清屏接口原文](https://github.com/KhronosGroup/OpenGL-Refpages/blob/main/gl4/glClearBuffer.xml)

## 查资料与接入工具

已有工具能回答的问题直接用现成入口。本次用开源的 renderdoc-mcp v0.3.0 打开原始帧、列出绘制事件，再用 RenderDoc 1.47 的回放 API 检查附件和硬件计数，事件面板截图保留选中的实际调用。引擎的 CPU 埋点和 OpenGL 的时间查询回答不同问题，计时时不加载截帧工具。[renderdoc-mcp 发布记录](https://github.com/JiaboLi-GitHub/renderdoc-mcp/releases/tag/v0.3.0) · [RenderDoc 事件面板](https://github.com/baldurk/renderdoc/blob/v1.x/docs/window/event_browser.rst) · [Khronos 计时查询](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_timer_query.txt)

遇到缺少的能力，可以查找和下载合适的开源工具或技能。先看上游仓库、许可、版本和它实际需要的访问范围，拿一个已有结果的项目样例试用。采用时在这里补上触发条件、版本、可复现入口和它补足的能力；与现有工具重复或在本项目中结果不可靠，也留下简短理由。设计和公式引用的章节仍归入[图形学资料索引](Graphics-References.md)。

## 什么时候同步给用户

新工具在项目样例上验证可用时，说明它替代了哪项手工工作。排错改变了原来的判断时，给出事件号或像素读数。优化结果持平、变慢，或只对某一类场景有效时，说明测试条件和下一次怎样验证。最后把源码、原始数据、编译后的文档放在同一份交付里，让下一次工作可以从已有证据接着做。
