# RHI 方案的插图

下列插图均用 TikZ 绘制，PDF 中的名称、箭头和数字可直接放大。现有类按仓库的成员与调用绘制，拟议接口在图和正文中单独标明。

| 图 | 源码 | 图中内容 |
| --- | --- | --- |
| 1 | [interfaces.tex](interfaces.tex) | 从首次加载到每帧显示。箭头标明下一步收到的模型、绘制数据或图像。 |
| 2 | [model-classes.tex](model-classes.tex) | AssetManager 按编号查询 AssetLibrary，首次加载后缓存 Model。Model 包含 SubMesh，子网格持有 Mesh 与 Material。 |
| 3 | [mesh-shader-resources.tex](mesh-shader-resources.tex) | Mesh、VertexArray 和两种缓冲区的持有关系，以及 3D 材质、2D 子贴图、ShaderLibrary 和相机数据的使用方法。 |
| 4 | [rhi-architecture.tex](rhi-architecture.tex) | 从引擎调用者经过公共设备和命令列表，到 OpenGL 与 Vulkan 各自的创建和绘制方法。 |
| 5 | [classes.tex](classes.tex) | 拟议后端类。空心三角指向两种实现共同遵循的接口。 |
| 6 | [backend-resources.tex](backend-resources.tex) | 纹理、缓冲区、着色器与管线、交换链图像在 Vulkan 后端拟持有或借用哪些对象。 |
| 7 | [draw-calls.tex](draw-calls.tex) | 从 CreateTexture、CreateBuffer 的请求与返回，到 BeginPass、DrawIndexed、EndPass 和帧末提交。 |
| 8 | [cpu-gpu-draw.tex](cpu-gpu-draw.tex) | GLSL 编译、暂存缓冲区、命令录制、队列提交与 GPU 着色。图后的几页分别解释上传、管线创建、资源绑定和索引绘制的实参。 |
| 9 | [effect-resources.tex](effect-resources.tex) | 按 SceneRenderer、IBLPrecompute 和 PostProcess 的实际成员与调用画出阴影、环境光、间接光历史、泛光和输出图。 |
| 10 | [window-targets.tex](window-targets.tex) | ForgeLayer 的离屏画面如何显示在窗口中。并列展示现有 OpenGL 上下文和拟议 Vulkan 交换链的工作。 |
| 11 | [texture-lifetime.tex](texture-lifetime.tex) | 面板拖大后，CPU 怎样知道最后一次使用小图的 GPU 提交已经完成。 |
| 12 | [shared-model.tex](shared-model.tex) | 两个实体用同一模型编号取得同一个已加载模型，实体编号仍然各不相同。按现有组件和 AssetManager 中的缓存绘制。 |
| 13 | [data-device.tex](../data-device.tex) | 暂存缓冲区中的两段数据。六个 32 位索引占 24 字节，四个 RGBA8 像素占之后的 16 字节。 |
| 14 | [frame-images.tex](frame-images.tex) | 工作帧 0 可以取得窗口图像 2。两个数组分别由 CPU 和交换链选取，取图与显示信号的归属对照 [Khronos 呈现信号复用说明](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html)。 |
| 15 | [clip-depth.tex](clip-depth.tex) | 同一顶点在 OpenGL 与 Vulkan 约定下的深度。三个刻度由附录 A 的公式计算。 |

## 编辑器与离屏测试的实测图

本次接入实录的三张图均为 RenderDoc 1.47 导出的原始像素。[正式编辑器窗口](../../../../output/rhi/forge-cornell-window.png) 和[场景面板颜色](../../../../output/rhi/forge-cornell-panel.png) 来自 RedflagForge 的 Cornell Box 第 66 帧。场景绘制为事件 19–49，窗口界面绘制为事件 60–72。[离屏测试颜色图](../../../../output/rhi/mesh-depth-color.png) 来自 `RHITests` 的 32 × 16 目标，绘制事件为 13 和 18。用 `tests/run_rhi_tests.cmd` 可以复核同一组颜色与实体编号。

[正式编辑器第 66 帧](../../../../output/captures/Forge-Vulkan-Cornell-66.rdc) 与[离屏像素对照](../../../../output/captures/RHI_capture.rdc) 均可用 RenderDoc 1.47 回放。前者 SHA-256 为 `7934fd4700485e61b7aacdd6eed1490289af05ef48ac5e97e61a2d449da9aa83`，后者为 `1e5ddc11d7a50d02ad999a35a4616c5373f3eb3c0009c1057aa7720edc05dd60`。本机 renderdoc-mcp 0.3.0 内置回放程序不能读取该版本的 Vulkan 捕获，导出图像用的是同版本的 qrenderdoc。

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

“场景几何与鼠标拾取 / 场景光照与屏幕效果”据本仓库 SceneRenderer、VulkanForgeLayer、LightingPreview 的当前调用关系绘制。

资源释放一节并列刊用 [Khronos Vulkan Samples 的 wait_idle 原图与说明](https://github.com/KhronosGroup/Vulkan-Samples/tree/0a97dba1e71b8fa191afb012b784ebad477ccf5a/samples/performance/wait_idle)。两张 PNG 为原文件，按示例的 [Apache 2.0 许可证](khronos-wait/LICENSE.txt) 分发，作者为 Arm 和示例贡献者。这是 Mali 平台的示例时间线。

## 中庭的四张图与纹理采样

[挂毯的颜色、深度和实体编号](sponza-attachments.tex)来自同一次 Vulkan 回放。场景、可执行文件、捕获摘要与调用列表见[第 10 帧的记录](../../../../output/rhi/editor-sponza-events.json)。在 RenderDoc 1.47 停到事件 638，底色、实体编号和深度依次是当前帧缓冲的附件 0、1 和 2，资源编号分别为 3981、3985 和 3989。事件 108 生成的面板显示图为资源 4032。四图源尺寸均为 1393 × 878，A 和 B 使用从左上角开始计数的像素 (80, 600) 和 (694, 28)。四幅 PNG 仅为排版降采样至 1024 × 645。底色取原 RGB，显示图按其显示值量化为 8 位。深度灰度按 `clamp((1 - depth) / 0.045, 0, 1)` 换算。编号 4 着青色，−1 着米色，原始编号图中只出现这两个值。场景来自 [Khronos glTF Sample Assets 中的 Sponza](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/e124e8d8d7330a5372bc2b4d00d39a4a9d7997ea/Models/Sponza)，作者及许可见该模型目录。

“同一张贴图的两种读法”使用 Alexander Overvoorde 与 The Khronos Group 的[官方教程原图](https://raw.githubusercontent.com/KhronosGroup/Vulkan-Tutorial/main/images/texture_filtering.png)，所在章节为 [Image view and sampler / Samplers](https://docs.vulkan.org/tutorial/latest/06_Texture_mapping/01_Image_view_and_sampler.html)。2026-11-22 下载，保留原图，未修改像素。图片按 [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) 使用，文件级依据见 [REUSE.toml 中 images/** 的条目](https://github.com/KhronosGroup/Vulkan-Tutorial/blob/main/REUSE.toml)。
