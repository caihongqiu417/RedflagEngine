# 外部示例图

以下两图下载于 2026-11-21，原始像素未修改，文档中仅缩放显示。它们说明参考教程的组织方式，不表示 Redflag 已实现对应系统。Redflag 的目标架构与资源生命周期图另外用 TikZ 绘制。

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

《游戏引擎架构》本轮核对了 Jason Gregory 作者站的版次和主题介绍，未下载整本书，也未使用未经核实的书中图号或页码。
