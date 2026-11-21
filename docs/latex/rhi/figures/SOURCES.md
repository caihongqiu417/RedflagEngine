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
| 8 | [cpu-gpu-draw.tex](cpu-gpu-draw.tex) | GLSL 编译、资源上传、命令录制与队列提交，以及 GPU 上的顶点着色、光栅化和片元着色。 |
| 9 | [effect-resources.tex](effect-resources.tex) | 按 SceneRenderer、IBLPrecompute 和 PostProcess 的实际成员与调用画出阴影、环境光、间接光历史、泛光和输出图。 |
| 10 | [window-targets.tex](window-targets.tex) | ForgeLayer 的离屏画面如何显示在窗口中。并列展示现有 OpenGL 上下文和拟议 Vulkan 交换链的工作。 |
| 11 | [texture-lifetime.tex](texture-lifetime.tex) | 面板拖大后，CPU 怎样知道最后一次使用小图的 GPU 提交已经完成。 |
| 12 | [clip-depth.tex](clip-depth.tex) | 同一顶点在 OpenGL 与 Vulkan 约定下的深度。三个刻度由附录 A 的公式计算。 |

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
