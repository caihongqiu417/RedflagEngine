# RHI 方案的插图

正文三幅图用 TikZ 绘制，中文与数字在 PDF 中均可选取、放大。每幅图的调用、时间或坐标都在正文相应段落解释。

| 图 | 源码 | 图中内容 |
| --- | --- | --- |
| 1 | [interfaces.tex](interfaces.tex) | Redflag 拟议的纹理创建与模型绘制接口，以及各自选中的后端实现。 |
| 2 | [texture-lifetime.tex](texture-lifetime.tex) | 假设帧 A 是最后一个使用旧视口纹理的提交。调整面板后新旧纹理并存，直到显卡完成帧 A。 |
| 3 | [clip-depth.tex](clip-depth.tex) | 同一顶点在 OpenGL 与 Vulkan 约定下的深度。三个刻度直接由正文公式计算。 |

## Khronos 的参考原图

下面两张 PNG 于 2026-11-21 从教程下载，保留原始像素，可按原尺寸单独打开。正文使用上面的中文示意图。

| 文件 | 原图与所在章节 | 用于说明 |
| --- | --- | --- |
| `khronos-layers.png` | Khronos Vulkan Tutorial, Building a Simple Engine, [Engine Architecture: Architectural Patterns](https://docs.vulkan.org/tutorial/latest/Building_a_Simple_Engine/Engine_Architecture/02_architectural_patterns.html)；[原图](https://docs.vulkan.org/tutorial/latest/_images/images/layered_architecture_diagram.png) | 将平台实现与上层场景、渲染职责分开；Redflag 按自己的资源依赖重新画图，不照搬原图的双向箭头 |
| `khronos-rendering.png` | 同系列，[Engine Architecture: Rendering Pipeline](https://docs.vulkan.org/tutorial/latest/Building_a_Simple_Engine/Engine_Architecture/05_rendering_pipeline.html)；[原图](https://docs.vulkan.org/tutorial/latest/_images/images/rendering_pipeline_flowchart.png) | 区分可见性、Pass 编排、命令生成与提交；RenderGraph 是以后可选的调度层 |

两图版权署名：© 2023–2026 The Khronos Group, Inc.；许可 [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/)。文件级依据为官方仓库 [REUSE.toml](https://github.com/KhronosGroup/Vulkan-Tutorial/blob/main/REUSE.toml) 中 `images/**` 和对应文件名的条目。该图像许可不同于 Building a Simple Engine 章节文字的 Apache-2.0，不能混写。

校验值（SHA-256）：

```text
khronos-layers.png
6e82461892e127cd419e4dbf7bb84948db8050030722bfc69c967d4df326d685
khronos-rendering.png
e99a0fe5b701c3965a0eb750a3dc79100df4304c399a21e54402956b2b21cbe2
```

GAMES104 第 2 讲课件另从[课程官网](https://games104.boomingtech.com/sc/course-list/)链接的 [BoomingTech CDN](https://cdn.boomingtech.com/games104_static/upload/GAMES104_lecture02Layered%20Architecture%20of%20Game%20Engine.pdf) 下载到临时资料目录，核对了 PDF 第 12–14、26、30 页的资源与 RHI 内容。本设计没有嵌入该课件整页，没有宣称取得课件和其中第三方配图的再分发授权。

《游戏引擎架构》所据的首版书摘、第四版目录与其他参考资料的实际查阅范围，见[图形学与引擎教材](../../../Graphics-References.md)。
