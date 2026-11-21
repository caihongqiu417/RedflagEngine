# Redflag 引擎文档

## 从模型到屏幕

想了解一帧怎样画出来，可以先读[经典场景实测](../../output/pdf/Redflag-Classic-Scenes.pdf)。在 Cornell Box 的红墙旁找一小块白地面，比较开启和关闭间接光时它的颜色。接着读[渲染公式与实现](../../output/pdf/Redflag-Rendering-Math.pdf)，同一块地面会再次出现。这次可以顺着光的方向，把读到的红色增量对回公式和着色器。书中配图的[几何与材质来源](math/figures/geometry-sources.md)和[光照与后处理来源](math/figures/effects-sources.md)收在源码旁。

[引擎架构](../../output/pdf/Redflag-Architecture-LaTeX.pdf) 介绍模型的加载、可见物体的选择，以及编辑器怎样取得渲染结果。[RHI 与 Vulkan 方案](../../output/pdf/Redflag-RHI-Vulkan-Plan.pdf) 结合贴图工厂和后处理说明接口怎样封装，并用三张图讲解资源创建与绘制、视口纹理何时释放，以及深度换算。图的[可编辑源码和参考原图](rhi/figures/SOURCES.md)放在一起，书籍、讲义和引擎源码的阅读范围见[资料索引](../Graphics-References.md)。

遇到问题时，先从[排错记录](../../output/pdf/Redflag-Debugging-Notes.pdf)找相似的画面。旧错误的代码、修正过程与能回放的帧放在一起。[技术选型](../../output/pdf/Redflag-Technical-Decisions.pdf) 则解释为什么采用这种实现。涉及性能的章节给出采样条件、实际 CPU 时间线，以及当时还能从数据里得出什么结论。

## LaTeX 源码与编译

在本目录用 PowerShell 运行对应的脚本。每个脚本调用两次 XeLaTeX，使交叉引用和图号更新，再将 PDF 放入 `output/pdf/`。中间文件留在各册的 `build/` 目录。TeX Live 需包含 ctex、TikZ、Fandol 和 TeX Gyre 字体。

| 文档 | 可编辑的主文件 | 编译命令 |
| --- | --- | --- |
| 引擎架构 | [main.tex](main.tex) | `./build.ps1` |
| 渲染公式与实现 | [math/main.tex](math/main.tex) | `./math/build.ps1` |
| RHI 与 Vulkan | [rhi/main.tex](rhi/main.tex) | `./rhi/build.ps1` |
| 排错记录 | [debugging/main.tex](debugging/main.tex) | `./debugging/build.ps1` |
| 技术选型 | [decisions/main.tex](decisions/main.tex) | `./decisions/build.ps1` |
| 经典场景实测 | [scenes/main.tex](scenes/main.tex) | `./scenes/build.ps1` |

架构图可以直接在 LaTeX 中修改。数学册中的[核验脚本](math/verify.py)会复算书中的数值例，并逐段对照引用的着色器或 C++ 源文件。从仓库根目录运行 `python docs/latex/math/verify.py` 即可。实测使用的场景截图、RenderDoc 回放结果和 CPU 采样记录分别保存在 [scenes](../../output/scenes/)、[captures](../../output/captures/) 和 [profiling](../../output/profiling/) 目录。
