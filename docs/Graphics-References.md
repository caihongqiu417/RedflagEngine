# 图形学与引擎教材

## 单机游戏的界面设计

《死亡搁浅》的界面设计师 Casey Matsumoto 在作品页展示了实际界面，也列出了他参与的生命、任务和进度显示。可以先看图标本身，再看数字和文字如何靠近它。同一项任务的几个部分即使直接放在画面上，也要让人一眼认出它们属于同一组。维修站把工单图标、任务名和下一步操作排在左上角，生命、装备和物资另放在左下角。[设计师的作品和工作说明](https://www.caseymatsumoto.com/death-stranding/)

《地平线 西之绝境》的开发团队 Guerrilla 介绍了玩家如何选择 HUD 上哪些信息可见，以及何时显示。拿维修站试这个思路，可以从急救工具箱旁走过一次。走近时中央提示告诉玩家能按 E 做什么，离开后同一个位置让给场景。队友的名字和生命条跟着角色移动，屏幕边缘则留给本局任务。[Guerrilla 的界面与操作介绍](https://blog.playstation.com/2022/02/10/accessibility-features-in-horizon-forbidden-west/)

《战神 诸神黄昏》的官方手册把常显、按手势查看和隐藏列为多种 HUD 元素的选项。这里值得借鉴的是玩家想查时能随时找回来。维修站的右下角保留 F1 提示，按住能看到完整按键，松开继续看场景。这是维修站采用的键鼠操作。改字号、颜色或文字阴影时，在天空、浅墙和棚内各看一次，尤其留意细小的中文笔画。[Santa Monica Studio 的 HUD 设置说明](https://www.playstation.com/en-us/games/god-of-war-ragnarok/accessibility/) · [维修站布局和图片来源](../RedflagForge/assets/ui/station/README.md)

## 实时光照的交叉阅读

同一根石柱可以连接几门课。FCG 的相机变换和 GAMES101 的投影解释石柱的边缘落在哪个像素；Karis 的课程接着说明怎样跨帧使用这些采样。项目的 TAA 笔记把两步放在一起，推导出“显示像素的运动 = 两次稳定投影的位置差”。PBRT 第四版 §2.1 的两灯估计解释选中一盏灯后为什么要除以概率；ReSTIR DI 把这项权重带进蓄水池，ReSTIR GI 再用 §2.4 的概率换元，把反射方向变成柱面上的二次交点。[每章的仓库、论文、课程和源码入口](latex/lighting/README.md) · [PBRT 的概率换元](https://pbr-book.org/4ed/Monte_Carlo_Integration/Transforming_between_Distributions) · [Karis 的 SIGGRAPH 课程](https://advances.realtimerendering.com/s2014/)

比较相邻路线时，AMD GI-1.0 的辐亮度缓存和 Godot 的 SDFGI 提供了不同的场景表示，可以对照 DDGI 的探针范围和光线怎样寻找遮挡物。项目已实现的七项方法、采用的近似和各自的误差都写在分章笔记里。[AMD Capsaicin](https://github.com/GPUOpen-LibrariesAndSDKs/Capsaicin) · [Godot 关于有符号距离场全局光照的说明](https://docs.godotengine.org/en/stable/tutorials/3d/global_illumination/using_sdfgi.html)

这份索引记录 Redflag 设计和验证渲染器时用到的资料。下面的阅读记录写到具体函数、章节或讲义页码。本次按问题选读了这些内容，尚未通读整套教材或逐文件审查两个引擎仓库。后面的书目也保留了供日后查阅的主题。

## RHI 方案的阅读记录（2026-11-21）

**资源创建与绘制。** Hazel 的两个 `Texture2D::Create` 分别接收纹理描述和文件路径，在函数内选择后端并返回 `Ref<Texture2D>`。`RenderCommand::DrawIndexed` 接到绘制请求便转给 `s_RendererAPI`。Redflag 借用前一种写法，让材质持有公共纹理类型。方案中新加的命令记录和提交功能由 Redflag 自己设计。[Hazel Texture.cpp](https://github.com/TheCherno/Hazel/blob/1feb70572fa87fa1c4ba784a2cfeada5b4a500db/Hazel/src/Hazel/Renderer/Texture.cpp) · [RenderCommand.h](https://github.com/TheCherno/Hazel/blob/1feb70572fa87fa1c4ba784a2cfeada5b4a500db/Hazel/src/Hazel/Renderer/RenderCommand.h)

**引擎资源与显卡资源。** GAMES104 第 2 讲第 12–14 页介绍资源导入、标识和运行时的引用，第 26 页用多个图形 API 说明 RHI 的用途，第 30 页给出上层依赖下层接口的原则。配套引擎 Piccolo 在 `RenderSystem::initialize` 中先创建 `VulkanRHI`，再将它传给 `RenderResource`，由后者上传环境光照纹理。在 Redflag，场景里的资源 ID 用来找回模型，绘制时用到的顶点和贴图则由当前后端创建。[GAMES104 第 2 讲](https://cdn.boomingtech.com/games104_static/upload/GAMES104_lecture02Layered%20Architecture%20of%20Game%20Engine.pdf#page=12) · [Piccolo render_system.cpp](https://github.com/BoomingTech/Piccolo/blob/f5053707fed4d3f94d270a436fb0d3a8ae54e3e5/engine/source/runtime/function/render/render_system.cpp) · [rhi.h](https://github.com/BoomingTech/Piccolo/blob/f5053707fed4d3f94d270a436fb0d3a8ae54e3e5/engine/source/runtime/function/render/interface/rhi.h)

**模型实例与场景渲染器。** Jason Gregory 在《游戏引擎架构》首版（2009）第 14 章 §14.6.2 用网格实例讲分工。游戏对象改变实例的位置、可见性和材质，渲染系统等对象更新完，再绘制可见实例。这个例子说明了为什么 Redflag 可以在场景中保留实体数据，由场景渲染器集中决定要画什么。第四版卷二另有第 11 章 *Rendering* 和第 12 章 *Lighting and Post-Processing*，本轮核对了这两章的目录，正文仍待阅读。[首版作者书摘](https://www.gamedeveloper.com/programming/book-excerpt-game-engine-architecture) · [第四版卷二目录](https://www.routledge.com/Game-Engine-Architecture-Volume-II-Graphics-Motion-and-Sound/Gregory/p/book/9781041162575)

**场景颜色与后处理。** LearnOpenGL 的 *Framebuffers* 先将场景画进一张纹理，下一次绘制再读取它。Unity 6.0 的 URP 示例给这两张图各自指定用途。`UseTexture(source)` 声明要读取的图，`SetRenderAttachment(destination, 0)` 指定结果写到哪里，`SetRenderFunc` 指定实际执行的函数。Redflag 可以让后处理直接接收场景颜色和输出图，在现有固定绘制顺序里把这两个参数讲清楚。[LearnOpenGL：Framebuffers](https://learnopengl.com/Advanced-OpenGL/Framebuffers) · [Unity 6.0：Write a render pass](https://docs.unity.com/en-us/engine/6000.0/manual/render-pipelines/universal-render-pipeline/customizing-urp/render-graph/write-render-pass)

**Vulkan 的创建、绘制与呈现。** Khronos 的 *Creating an instance* 示例从 GLFW 取得窗口系统所需的扩展。*Building a Simple Engine* 的渲染流程章把场景收集、可见性判断和命令提交分开，也注明了示例引擎直接发起各次绘制。动态渲染示例在每次开始绘制时指明目标图像。Redflag 因此可以先让视口交出本次输出，让 Vulkan 后端填写自己的图像参数。[Instance](https://docs.vulkan.org/tutorial/latest/03_Drawing_a_triangle/00_Setup/01_Instance.html) · [Rendering Pipeline](https://docs.vulkan.org/tutorial/latest/Building_a_Simple_Engine/Engine_Architecture/05_rendering_pipeline.html) · [Dynamic rendering](https://docs.vulkan.org/samples/latest/samples/extensions/dynamic_rendering/README.html)

交换链还有一项独立约定。CPU 等到一帧绘制完成时，窗口系统可能仍在等待呈现这张图。Khronos 的示例给每张交换链图像配一个供呈现等待的信号量，重新取到同一张图并等到取图完成后，才复用它。Redflag 的 Vulkan 后端据此分别管理每帧提交的状态和每张图呈现所需的信号。[Khronos：Swapchain Semaphore Reuse](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html)

### 两种后端的画面验证

**从图理解受光面积。** 虎书《Fundamentals of Computer Graphics》第五版第 5 章图 5.2 把受光面转过 60°，图 5.3 给出法线与来光方向。§14.6–14.7 再讨论辐射度量和表面散射。Marschner 的公开讲义第 6 页推导迎光面积和斜面面积之间的换算。《渲染公式与实现》用卡片和半球网格另画了两幅图。使用者可据章节核对原例与单位。[虎书作者配套页](https://www.cs.cornell.edu/~srm/fcg5/) · [Marschner《Radiometry》第 6 页](https://www.cs.cornell.edu/courses/cs6630/2022fa/notes/radiometry-notes.pdf#page=6)

**从有限求和到积分。** OpenStax《Calculus》卷一 §5.1 讲把区间切成小段、用矩形面积近似曲线下的面积，§5.5 讲积分换元。卷三 §5.1 把求和扩展到二维小格。IBL 正文先量半球上一格的宽和高，再用常量环境图检查求和。向量投影、垂直关系和坐标变换可配 Strang 的线性代数课程；学习随机采样时，从一盏灯被抽中的概率算起，再读 PBRT 的期望与方差。[矩形求和](https://openstax.org/books/calculus-volume-1/pages/5-1-approximating-areas) · [换元](https://openstax.org/books/calculus-volume-1/pages/5-5-substitution) · [二重积分](https://openstax.org/books/calculus-volume-3/pages/5-1-double-integrals-over-rectangular-regions) · [MIT 18.06SC](https://ocw.mit.edu/courses/18-06sc-linear-algebra-fall-2011/) · [PBRT §2.1](https://pbr-book.org/4ed/Monte_Carlo_Integration/Monte_Carlo_Basics)

**引擎和厂商资料。** 编辑器讲义保留 Unity 对场景视图、层级和属性的区分，操作图来自 Redflag 实机。Godot 的 GI 概览把照明方案放到可编辑场景中比较，适合对照烘焙和动态光照的用法。DDGI 的探针状态、更新和 SDK 使用条件则应回到 NVIDIA 自己的仓库与其中的文档。[Unity 场景视图](https://docs.unity3d.com/Manual/UsingTheSceneView.html) · [Godot 全局光照](https://docs.godotengine.org/en/stable/tutorials/3d/global_illumination/introduction_to_global_illumination.html) · [NVIDIA RTXGI-DDGI](https://github.com/NVIDIAGameWorks/RTXGI-DDGI)

**顶点、深度和法线。** 《Fundamentals of Computer Graphics》第五版 §1.3–1.4（印刷页 4–5）介绍绘图接口、顶点怎样投到屏幕，以及深度缓冲怎样判定前后。作者的 Cornell 课程第 14 讲第 2–3 页画出了从模型到屏幕的各次变换。GAMES101 第 4 讲第 17、21–24 页接着说明如何把正交视体移到原点，再缩放到标准立方体。Redflag 对照两个后端时，可以给定同一个顶点，按各自 API 的深度约定换算后，再比较屏幕位置和深度。[FCG 第五版公开试读](https://api.pageplace.de/preview/DT0400.9781000426281_A41250965/preview-9781000426281_A41250965.pdf) · [Cornell CS4620 第 14 讲](https://www.cs.cornell.edu/courses/cs4620/2014fa/lectures/14pipeline.pdf#page=2) · [GAMES101 第 4 讲](https://sites.cs.ucsb.edu/~lingqi/teaching/resources/GAMES101_Lecture_04.pdf#page=17)

《Real-Time Rendering》第四版附录 A.4（印刷页 15–16）用齐次坐标区分位置和方向，并介绍透视除法。PBRT 第四版 §3.10.3 则从法线应与切线垂直推导出逆转置矩阵。若同一模型在两个后端轮廓一致、明暗却不同，可以用带非均匀缩放的平面核对法线。[RTR4 附录 A.4](https://www.realtimerendering.com/Real-Time_Rendering_4th-Appendices.pdf#page=19) · [PBRT 第四版：Applying Transformations](https://pbr-book.org/4ed/Geometry_and_Transformations/Applying_Transformations)

**反射阴影图（RSM）。** GAMES202 第 7 讲第 32–39 页从受直射光的表面出发，解释这些表面怎样给其他点提供间接光。第 39 页将光源视角的深度、世界位置、法线和反射通量并排展示。Redflag 在 Cornell Box 核对彩色反光，也可以先查红墙是否出现在这些图里，再检查白墙接收的间接光。第 41 页还指出了采样误差和间接光遮挡的限制。[GAMES202 第 7 讲](https://sites.cs.ucsb.edu/~lingqi/teaching/resources/GAMES202_Lecture_07.pdf#page=32) · [项目的 RSM 推导](latex/math/rsm.tex)

## 坐标、光照与渲染教材

| 教材 | 优先用于 | 官方入口与版次 |
| --- | --- | --- |
| LearnOpenGL | OpenGL 实践；变换、相机、纹理、阴影、HDR、Bloom、SSAO、PBR 与 IBL 的入门和代码对应 | [原站](https://learnopengl.com/)、[PBR Theory](https://learnopengl.com/PBR/Theory)；以所引用网页为准 |
| Physically Based Rendering: From Theory to Implementation（PBRT） | 辐射度量、BRDF/BSDF、渲染方程、概率密度、蒙特卡洛积分、重要性采样与光传输 | [官方入口](https://pbr-book.org/)、[第四版目录](https://pbr-book.org/4ed/contents)；用户未指定版次，默认查公开第四版并标明 |
| Real-Time Rendering, Fourth Edition（RTR4） | 实时渲染管线、着色、阴影、纹理过滤、抗锯齿、全局光照和性能取舍 | [作者网站](https://www.realtimerendering.com/)、[出版社页面](https://www.routledge.com/Real-Time-Rendering/Akenine-Moller-Haines-Hoffman-Pesce-Iwanicki-Hillaire/p/book/9781138627000)；固定第四版 |
| Khronos Vulkan Tutorial | Vulkan 对象、资源生命周期、管线、命令提交与同步；比较 OpenGL 隐式状态和 Vulkan 显式管理 | [教程入口](https://docs.vulkan.org/tutorial/latest/00_Introduction.html)、[用户指定的 Instance 章节](https://docs.vulkan.org/tutorial/latest/03_Drawing_a_triangle/00_Setup/01_Instance.html)；latest 会变化，使用时核对版本 |
| Fundamentals of Computer Graphics（FCG） | 向量矩阵、几何、坐标变换、投影、光栅化与光线追踪的基础概念和推导 | [作者第5版配套网站](https://www.cs.cornell.edu/~srm/fcg5/)、[出版社第5版页面](https://www.routledge.com/Fundamentals-of-Computer-Graphics/Marschner-Shirley/p/book/9781032122861)；本轮使用第五版公开试读和作者课件 |
| Game Engine Architecture（Jason Gregory，《游戏引擎架构》） | 引擎分层、平台抽象、资源管线、资源何时创建和释放、编辑器怎样调用渲染器 | [作者网站](https://gameenginebook.com/)、[出版社第四版两卷本](https://www.routledge.com/Game-Engine-Architecture-Two-Volume-Set/Gregory/p/book/9781041162599)；作者站目前介绍第四版。用户未指定版次，引用时写明版本，不将第三版页码套入第四版 |

## 课程与引擎源码

| 来源 | 本项目使用范围 | 核实的入口 |
| --- | --- | --- |
| GAMES101：现代计算机图形学入门，闫令琪 | 变换、光栅化、着色与采样的基础；理解相机、顶点布局和纹理约定 | [教师课程页（2020）](https://sites.cs.ucsb.edu/~lingqi/teaching/games101.html)、[GAMES 入口](https://games-cn.org/intro-graphics/)；第 3–9 讲对应变换、光栅化和着色。课程讲原理，不是 OpenGL/Vulkan API 教程 |
| GAMES202：高质量实时渲染，闫令琪 | 阴影、RSM、SSAO、IBL、时域积累及降噪；给 Redflag 的 Pass 输入输出和历史资源约束找依据 | [教师课程页（2021）](https://sites.cs.ucsb.edu/~lingqi/teaching/games202.html)、[GAMES 入口](https://games-cn.org/games202/)；课程列出 RSM、SSAO、SSR 和时域主题，推荐 RTR4 |
| GAMES104：现代游戏引擎：从入门到实践，王希 | 引擎分层、资源管理、渲染硬件接口及渲染管线；对照项目里的模型加载和绘制调用 | [课程官网](https://games104.boomingtech.com/sc/)、[官方课件与视频](https://games104.boomingtech.com/sc/course-list/)、[GAMES 课件目录](https://games-cn.org/games104-slides/)；重点第 2 讲分层、第 4 讲渲染基础、第 7 讲管线与后处理 |
| Khronos：Building a Simple Engine | 将单个 Vulkan 示例组织为渲染引擎；资源管理、管线与 ImGui 接入的拆分 | [用户指定目录](https://docs.vulkan.org/tutorial/latest/Building_a_Simple_Engine/)、[可直接访问的 Introduction](https://docs.vulkan.org/tutorial/latest/Building_a_Simple_Engine/introduction.html)、[Engine Architecture](https://docs.vulkan.org/tutorial/latest/Building_a_Simple_Engine/Engine_Architecture/01_introduction.html)；先完成基础 Vulkan 教程再读，不直接移植其 C++20 modules 构建 |
| TheCherno/Hazel 公开源码 | 轻量接口、工厂和资源对象的命名，接口与平台实现分离 | [RendererAPI.h](https://github.com/TheCherno/Hazel/blob/master/Hazel/src/Hazel/Renderer/RendererAPI.h)、[Shader.h](https://github.com/TheCherno/Hazel/blob/master/Hazel/src/Hazel/Renderer/Shader.h)、[Texture.h](https://github.com/TheCherno/Hazel/blob/master/Hazel/src/Hazel/Renderer/Texture.h)；只把实际查到的代码归于 Hazel，稳定 AssetID 与 Vulkan RHI 属于 Redflag 的设计 |
| BoomingTech/Piccolo | 对照 GAMES104 中资源、渲染与 RHI 的实际组织 | [课程配套源码](https://github.com/BoomingTech/Piccolo)；用于比较职责和调用关系，不把其全部类型包装照搬进 Redflag |

GAMES101/202/104 分别补基础原理、实时效果和引擎组织，三者不能相互替代。当前任务集中在资源与渲染接口，暂不扩展到角色动画、物理或网络课程。

## 按问题选教材

- **矩阵、坐标变换、投影为什么这样写**：先用 FCG 建立几何直觉，再用 LearnOpenGL 对照实际代码。
- **光照公式、余弦项、立体角、BRDF 的来源**：以 PBRT 的定义和推导为主，再说明 RTR4 与 LearnOpenGL 中的实时近似。
- **采样、PDF、蒙特卡洛、重要性采样、MIS**：以 PBRT 为主，明确积分测度、采样分布和估计量；这里的 PDF 是概率密度，不是文档格式。
- **阴影、SSAO、Bloom、IBL 等实际效果**：先用 LearnOpenGL 解释实现，再查 RTR4 说明适用条件、误差和性能成本。
- **GPU 对象的持有与释放**。对照 Khronos 示例和项目现有的 Vulkan 绘制、计算与窗口呈现代码。更细粒度的异步回收单独标为后续设计。
- **引擎类名和封装方式**：仍以项目记忆中的 Hazel 源码约定为准。教材用于解释图形学，不自动替代现有工程规范。
- **资源层、Renderer 与 RHI 如何分工**：读《游戏引擎架构》的相关主题和 GAMES104 第 2 讲，再对照 Hazel 与 Piccolo。资产 ID 指向内容，RHI 对象表示 GPU 资源，帧内临时附件归渲染器管理。

## 从 OpenGL 场景到 Vulkan 窗口

先读 Hazel 的 RendererAPI 和 Texture，再对照 Redflag 中加载一张底色图的代码。两种引擎都给调用者一个可共享的纹理对象。接着翻到 Khronos 的开窗与呈现章节，把一个三角形画到自己的窗口里。这样再读它的引擎章节时，设备、图像和绘制命令都有具体的画面可以对应。

回到 Redflag，下一次替换的是一个带材质、可拾取的模型。先测颜色、深度和编号，再把模型图放进编辑器，最后接回阴影、环境光和后处理。GAMES104 对应引擎的组织，GAMES202 对应各项光照效果的计算。实施步骤和每一步要看到的画面见 [RHI 与 Vulkan 方案](latex/rhi/main.tex)。

## 公式与项目代码

不同问题适合不同的讲法。解释透视投影时，给一个顶点代入公式，读者就能核对它会落在哪个像素；介绍一张贴图的用途时，说明谁写它、谁读它就够了。两种后端产生不同画面时，可以把同一处像素的读数放在一起比较。

需要公式时，写清符号的含义、单位和适用条件，并在附近给出项目里真正执行的语句。来自不同书的式子要先统一入射方向、坐标系、深度范围和颜色空间。文中在推导或教材图旁给出相关章节，让读者可以从当前的问题查回原文。独立讲义采用 LaTeX，项目自己的示意图可用 TikZ 绘制。

## 着色器中的阴影、材质与后处理

本轮注释按“效果作用 → 直观步骤 → 变量与公式 → 输入输出”组织。先让读者知道这段着色器在画什么、为什么这样算，再解释坐标空间和实现细节。资源编号只在确实影响调用约定的位置注明，不以 slot/binding 清单开头。

| 效果 | 本轮核对的原文 | 注释需要讲清的内容 |
| --- | --- | --- |
| Bloom | [LearnOpenGL：Bloom](https://learnopengl.com/Advanced-Lighting/Bloom) | 亮部怎样扩散到周围，再与原图合成；Redflag 的多级降采样/上采样是项目实现，不能说成教程原样算法 |
| SSAO | [LearnOpenGL：SSAO](https://learnopengl.com/Advanced-Lighting/SSAO) | 从表面附近的样本估计环境遮挡；解释采样空间、深度比较和半径。项目按整个采样核大小归一化，落在屏幕外的样本贡献为零，边缘的遮挡可能减弱 |
| 阴影 | [LearnOpenGL：Shadow Mapping](https://learnopengl.com/Advanced-Lighting/Shadows/Shadow-Mapping) | 先记录光源看到的最近深度，再判断当前点是否被遮挡；解释投影转换与偏移的用途 |
| PBR | 上表的 [LearnOpenGL：PBR Theory](https://learnopengl.com/PBR/Theory) | 区分辐射量、能量约束与微表面模型，说明材质参数对反射的影响，不只罗列函数名 |
| Tonemap | Krzysztof Narkowicz，[ACES Filmic Tone Mapping Curve](https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/)，2016-01-06 | 曲线将高动态范围亮度压到显示范围；项目采用的系数属于作者给出的 ACES 拟合曲线，不等于实现了完整 ACES 色彩管理流程 |

上述链接用于解释原理和核对公式。项目代码中的近似、采样次数、分支和数据布局仍按实际实现说明，不复制原文长段，也不把项目改法冒称教材结论。

## 排错与性能测量

| 主题 | 已核对入口 | 对应的项目问题 |
| --- | --- | --- |
| 资产身份 | [Unity 6.0：Asset metadata](https://docs.unity3d.com/6000.0/Documentation/Manual/AssetMetadata.html) | 区分资产 ID 与文件位置，移动时保留元数据。Redflag 当前只接通模型 ID，不能类比为完整 Unity 导入数据库 |
| 透明裁剪 | [LearnOpenGL：Blending](https://learnopengl.com/Advanced-OpenGL/Blending) | 区分丢弃像素与半透明混合，解释深度和颜色轮廓一致的前提 |
| 实例化 | [LearnOpenGL：Instancing](https://learnopengl.com/Advanced-OpenGL/Instancing) | 一次绘制多个相同网格实例；具体分组限制与性能仍以 Redflag 源码和测量为准 |
| RSM 通量与几何项 | Dachsbacher、Stamminger，[Reflective Shadow Maps，I3D 2005 原论文](https://users.soe.ucsc.edu/~pang/160/s13/proposal/mijallen/proposal/media/p203-dachsbacher.pdf)，[DOI](https://doi.org/10.1145/1053427.1053460)，§3.1–3.3、印刷页 204–205 | 已核对通量生成、距离因子与无间接遮挡假设。Redflag 另推导按投影面积存密度的等面积圆盘估计，不将该密度与论文的单纹素通量混用。原文只在本机阅读，不随项目再分发 |
| Lambert 系数 | [PBRT 第四版 §9.2：Diffuse Reflection](https://pbr-book.org/4ed/Reflection_Models/Diffuse_Reflection) | 核对漫反射 BRDF 中的反射率与 1/π，解释 RSM 发送和接收两端各自的系数 |
| GPU 对象释放 | [Khronos：glDeleteVertexArrays](https://registry.khronos.org/OpenGL-Refpages/gl4/html/glDeleteVertexArrays.xhtml) | VAO 的创建与删除必须配对，C++ 引用释放不自动删除未封装好的 GL 对象 |
| 深度读回 | [Khronos：glReadPixels](https://registry.khronos.org/OpenGL-Refpages/gl4/html/glReadPixels.xhtml) | 核对读取范围、格式与目标内存，区分 CPU 预览和 GPU 附件本身 |
| 截帧中的资源绑定 | [RenderDoc 官方文档：Pipeline State](https://github.com/baldurk/renderdoc/blob/v1.x/docs/window/pipeline_state.rst) | 在选定的绘制事件中核对着色器实际读到的相机矩阵、曝光值和纹理 |
| 附件像素检查 | [RenderDoc 官方文档：Inspect a pixel value](https://github.com/baldurk/renderdoc/blob/v1.x/docs/how/how_inspect_pixel.rst) | 选择正确附件与子资源，查看真实像素值，避免把显示范围调整当成数据变化 |
| CPU 时间线 | [Chrome DevTools：Performance features reference](https://developer.chrome.com/docs/devtools/performance/reference) | 查看导入时间线的事件与嵌套区间；Redflag 的 Instrumentor 计时属于 CPU 范围，不代替 GPU 时间查询 |

项目记录分别维护在 [排错与截帧](latex/debugging/main.tex) 和 [技术选型](latex/decisions/main.tex)。实际截图须标明捕获文件、事件、附件及观察范围，测试读回图片不能标作 RenderDoc 界面截图。截图不构成性能基准，未测量时只记录成本和待测项。

## 图像来源

- LearnOpenGL：优先查对应章节内的原图、图注和第三方署名。
- PBRT：查对应版次的在线章节图和图注，引用具体章节 URL。
- RTR4：[官方图片库](https://www.realtimerendering.com/figures.html)，引用时保留来源，并依据书中图注保留原作者署名。
- FCG：查作者配套网站和实际使用版次；未取得原图时可自行画解释图，不能编造书中图号。
- Vulkan：查 Khronos 教程对应页的原图和署名，不把其他版本的截图当作当前教程。
- GAMES：使用课程官方页面链接的课件，注明讲师、讲次及 PDF 页码。课件中的第三方配图仍按该图来源处理，公开课件不等于其中所有图片可任意再分发。
- 《游戏引擎架构》：按作者站与出版社核对版本；没有拿到相应版次正文时不编造章节页码，也不下载非官方整本扫描件。

每次引用都记录：**教材/网站、版次、章节、图号或页码（能核实才填）、原始链接、原图或重绘**。
实际使用时核对图像的授权条件。重绘写“据……重绘”，原创写“示意图”。引用来源不等于把原书整章或整本复制进资料库。

## RTR4 的附录与在线章节

- RTR4 [参考文献](https://www.realtimerendering.com/refs.html)。
- RTR4 [线性代数与三角学附录](https://www.realtimerendering.com/Real-Time_Rendering_4th-Appendices.pdf)。
- RTR4 [实时光线追踪在线章节](https://www.realtimerendering.com/Real-Time_Rendering_4th-Real-Time_Ray_Tracing.pdf)。
- RTR4 [碰撞检测在线章节](https://www.realtimerendering.com/Real-Time_Rendering_4th-Collision_Detection.pdf)。

## 引用教材的版次

- 引用纸书页码必须先确认版次；没有正文可查时，只给官方入口和能够核实的章节信息。
- PBRT 第三版和第四版的组织、符号及代码不同，不混用章号、公式编号和页码。
- 核对时 Khronos latest 教程采用 Vulkan 1.4、C++20、Vulkan-Hpp RAII，并使用 Slang。以后引用要重新核对，不直接把旧 C API 教程代码拼进该版本，也不因此擅自升级 Redflag 的 C++17 工程。
- Jason Gregory 作者站目前介绍第四版两卷本，新增或扩充 GPU 编程、渲染与光照、motion matching、C++23 等内容。第三版和第四版的章节组织不同；本项目尚未获得对应版次全文，本轮只据官方介绍和实际查阅的公开资料讨论，不引用未经核实的书中图号。
- 后续用户提供具体版本或本地书籍时，将文件位置与版次补入本目录。

## C++、计算机系统与编辑器

材质共享贴图、图像独占显存与析构顺序对照《C++ Primer》第五版第 12.1 节和第 13 章。可直接阅读出版社公开的[第 12 章样章](https://www.informit.com/content/images/9780321714114/samplepages/0321714113.pdf)。

按行读取像素、相邻地址和重复使用同一份数据可结合 Bryant 与 O’Hallaron 的 CS:APP 第三版第 6 章及 Cache Lab 来读。该书建立 CPU 存储层次的直觉，GPU 堆的实际位置按 Vulkan 报告核对。[作者目录与前言](https://csapp.cs.cmu.edu/3e/pieces/preface3e.pdf) · [Cache Lab](https://csapp.cs.cmu.edu/3e/labs.html)

编辑器的信息布局参考 Unity 的层级、场景、项目与属性窗口。操作手册用 Redflag 当前版本实拍。[Unity 界面](https://docs.unity3d.com/cn/2018.3/Manual/LearningtheInterface.html)
