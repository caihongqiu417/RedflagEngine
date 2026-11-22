# Redflag 引擎文档

第一次打开编辑器可以先看[图解用户手册](../../output/pdf/Redflag-Editor-Guide.pdf)。[常用接口源码读本](../../output/pdf/Redflag-Source-Guide.pdf)从窗口、材质、效果开关和鼠标拾取进入实际代码，查完整声明可用[离线 API 索引](../../output/api/index.html)。

## 从模型到屏幕

想了解一帧怎样画出来，可以先读[经典场景实测](../../output/pdf/Redflag-Classic-Scenes.pdf)。在 Cornell Box 的红墙旁找一小块白地面，比较开启和关闭间接光时它的颜色。接着读[渲染公式与实现](../../output/pdf/Redflag-Rendering-Math.pdf)，同一块地面会再次出现。这次可以顺着光的方向，把读到的红色增量对回公式和着色器。书中配图的[几何与材质来源](math/figures/geometry-sources.md)和[光照与后处理来源](math/figures/effects-sources.md)收在源码旁。

[引擎架构](../../output/pdf/Redflag-Architecture-LaTeX.pdf) 介绍模型的加载、可见物体的选择，以及编辑器怎样取得渲染结果。[RHI 与 Vulkan 方案](../../output/pdf/Redflag-RHI-Vulkan-Plan.pdf) 图解模型、材质、贴图、窗口和离屏图像的封装。分层图说明上层与后端的分工，调用图列出具体方法，CPU/GPU 流程图把 SPIR-V、上传、绘制和着色连起来。插图和阅读资料可查[图源说明](rhi/figures/SOURCES.md)与[资料索引](../Graphics-References.md)。

遇到问题时，可以从[排错记录](../../output/pdf/Redflag-Debugging-Notes.pdf)找相似的画面。这里把出错的代码放在对应的截帧旁边，方便对着一个像素检查。[技术选型](../../output/pdf/Redflag-Technical-Decisions-Updated.pdf)第 9 页记录了一个取舍：一种取样方法能让远处的铁丝重新出现，但慢慢移动镜头时，画面里多出了闪动的颗粒。

## LaTeX 源码与编译

[渲染性能实测](../../output/pdf/Redflag-Rendering-Performance.pdf) 记录深度预写、2D 批渲染和 3D 实例化的实际 RenderDoc 事件、离屏 CPU/GPU 计时，以及接下来值得做的对照。表格由原始 CSV 生成。整理手册时怎样用浏览器检查源码跳转、用刻意排坏的插图试一份 skill，可以看[《我用 Agent 开发 Redflag 的经验》](../AI-Workflow.md)。

在本目录用 PowerShell 运行对应的脚本。每个脚本调用两次 XeLaTeX，使交叉引用和图号更新，再将 PDF 放入 `output/pdf/`。中间文件留在各册的 `build/` 目录。TeX Live 需包含 ctex、TikZ、Fandol 和 TeX Gyre 字体。

| 文档 | 可编辑的主文件 | 编译命令 |
| --- | --- | --- |
| 用户手册 | [editor-guide/main.tex](editor-guide/main.tex) | `./editor-guide/build.ps1` |
| 常用接口源码读本 | [source-guide/main.tex](source-guide/main.tex) | `./source-guide/build.ps1` |
| 引擎架构 | [main.tex](main.tex) | `./build.ps1` |
| 实时光照与采样 | [分章笔记及原作者材料](lighting/README.md) | `python lighting/build_notes.py` |
| 渲染公式与实现 | [math/main.tex](math/main.tex) | `./math/build.ps1` |
| RHI 与 Vulkan | [rhi/main.tex](rhi/main.tex) | `./rhi/build.ps1` |
| 排错记录 | [debugging/main.tex](debugging/main.tex) | `./debugging/build.ps1` |
| 技术选型 | [decisions/main.tex](decisions/main.tex) | `./decisions/build.ps1` |
| 渲染性能实测 | [performance/main.tex](performance/main.tex) | `./performance/build.ps1`（需 Python 3） |
| 经典场景实测 | [scenes/main.tex](scenes/main.tex) | `./scenes/build.ps1` |
| 角色动画 | [animation/main.tex](animation/main.tex) | 进入 animation 目录，运行两次 `xelatex main.tex` |

架构图可以直接在 LaTeX 中修改。数学册中的[核验脚本](math/verify.py)会复算书中的数值例，并逐段对照引用的着色器或 C++ 源文件。从仓库根目录运行 `python docs/latex/math/verify.py` 即可。实测使用的场景截图、RenderDoc 回放结果和 CPU 采样记录分别保存在 [scenes](../../output/scenes/)、[captures](../../output/captures/) 和 [profiling](../../output/profiling/) 目录。
