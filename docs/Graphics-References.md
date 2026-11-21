# 图形学与引擎架构参考资料

用户指定的长期教材目录。用于图形学概念、公式推导、实现说明与配图引用。
本目录保存官方入口和使用约定，不是书籍全文镜像。来源核对日期：2026-11-21。

## 教材分工

| 教材 | 优先用于 | 官方入口与版次 |
| --- | --- | --- |
| LearnOpenGL | OpenGL 实践；变换、相机、纹理、阴影、HDR、Bloom、SSAO、PBR 与 IBL 的入门和代码对应 | [原站](https://learnopengl.com/)、[PBR Theory](https://learnopengl.com/PBR/Theory)；以所引用网页为准 |
| Physically Based Rendering: From Theory to Implementation（PBRT） | 辐射度量、BRDF/BSDF、渲染方程、概率密度、蒙特卡洛积分、重要性采样与光传输 | [官方入口](https://pbr-book.org/)、[第四版目录](https://pbr-book.org/4ed/contents)；用户未指定版次，默认查公开第四版并标明 |
| Real-Time Rendering, Fourth Edition（RTR4） | 实时渲染管线、着色、阴影、纹理过滤、抗锯齿、全局光照和性能取舍 | [作者网站](https://www.realtimerendering.com/)、[出版社页面](https://www.routledge.com/Real-Time-Rendering/Akenine-Moller-Haines-Hoffman-Pesce-Iwanicki-Hillaire/p/book/9781138627000)；固定第四版 |
| Khronos Vulkan Tutorial | Vulkan 对象、资源生命周期、管线、命令提交与同步；比较 OpenGL 隐式状态和 Vulkan 显式管理 | [教程入口](https://docs.vulkan.org/tutorial/latest/00_Introduction.html)、[用户指定的 Instance 章节](https://docs.vulkan.org/tutorial/latest/03_Drawing_a_triangle/00_Setup/01_Instance.html)；latest 会变化，使用时核对版本 |
| Fundamentals of Computer Graphics（FCG） | 向量矩阵、几何、坐标变换、投影、光栅化与光线追踪的基础概念和推导 | [作者第5版配套网站](https://www.cs.cornell.edu/~srm/fcg5/)、[出版社第5版页面](https://www.routledge.com/Fundamentals-of-Computer-Graphics/Shirley-Marschner/p/book/9780367505035)；用户未指定版次，第5版只是目前核实的入口 |
| Game Engine Architecture（Jason Gregory，《游戏引擎架构》） | 引擎分层、平台抽象、资源管线、运行时资源生命周期、渲染系统与工具的边界 | [作者网站](https://gameenginebook.com/)、[出版社第四版两卷本](https://www.routledge.com/Game-Engine-Architecture-Two-Volume-Set/Gregory/p/book/9781041162599)；作者站目前介绍第四版。用户未指定版次，引用时写明版本，不将第三版页码套入第四版 |

## 课程与引擎源码

| 来源 | 本项目使用范围 | 核实的入口 |
| --- | --- | --- |
| GAMES101：现代计算机图形学入门，闫令琪 | 变换、光栅化、着色与采样的基础；理解相机、顶点布局和纹理约定 | [教师课程页（2020）](https://sites.cs.ucsb.edu/~lingqi/teaching/games101.html)、[GAMES 入口](https://games-cn.org/intro-graphics/)；第 3–9 讲对应变换、光栅化和着色。课程讲原理，不是 OpenGL/Vulkan API 教程 |
| GAMES202：高质量实时渲染，闫令琪 | 阴影、RSM、SSAO、IBL、时域积累及降噪；给 Redflag 的 Pass 输入输出和历史资源约束找依据 | [教师课程页（2021）](https://sites.cs.ucsb.edu/~lingqi/teaching/games202.html)、[GAMES 入口](https://games-cn.org/games202/)；课程列出 RSM、SSAO、SSR 和时域主题，推荐 RTR4 |
| GAMES104：现代游戏引擎：从入门到实践，王希 | 引擎分层、资源管理、渲染硬件接口及渲染管线；结合项目代码讨论边界 | [课程官网](https://games104.boomingtech.com/sc/)、[官方课件与视频](https://games104.boomingtech.com/sc/course-list/)、[GAMES 课件目录](https://games-cn.org/games104-slides/)；重点第 2 讲分层、第 4 讲渲染基础、第 7 讲管线与后处理 |
| Khronos：Building a Simple Engine | 将单个 Vulkan 示例组织为渲染引擎；资源管理、管线与 ImGui 接入的拆分 | [用户指定目录](https://docs.vulkan.org/tutorial/latest/Building_a_Simple_Engine/)、[可直接访问的 Introduction](https://docs.vulkan.org/tutorial/latest/Building_a_Simple_Engine/introduction.html)、[Engine Architecture](https://docs.vulkan.org/tutorial/latest/Building_a_Simple_Engine/Engine_Architecture/01_introduction.html)；先完成基础 Vulkan 教程再读，不直接移植其 C++20 modules 构建 |
| TheCherno/Hazel 公开源码 | 轻量接口、工厂和资源对象的命名，接口与平台实现分离 | [RendererAPI.h](https://github.com/TheCherno/Hazel/blob/master/Hazel/src/Hazel/Renderer/RendererAPI.h)、[Shader.h](https://github.com/TheCherno/Hazel/blob/master/Hazel/src/Hazel/Renderer/Shader.h)、[Texture.h](https://github.com/TheCherno/Hazel/blob/master/Hazel/src/Hazel/Renderer/Texture.h)；只把实际查到的代码归于 Hazel，稳定 AssetID 与 Vulkan RHI 属于 Redflag 的设计 |
| BoomingTech/Piccolo | 对照 GAMES104 中资源、渲染与 RHI 的实际组织 | [课程配套源码](https://github.com/BoomingTech/Piccolo)；用于比较职责和调用关系，不把其全部类型包装照搬进 Redflag |

GAMES101/202/104 分别补基础原理、实时效果和引擎组织，三者不能相互替代。当前任务集中在资源与渲染接口，暂不扩展到角色动画、物理或网络课程。

## 按问题选教材

- **矩阵、坐标变换、投影为什么这样写**：先用 FCG 建立几何直觉，再用 LearnOpenGL 对照实际代码。
- **光照公式、余弦项、立体角、BRDF 的来源**：以 PBRT 的定义和推导为主，再说明 RTR4 与 LearnOpenGL 中的实时近似。
- **采样、PDF、蒙特卡洛、重要性采样、MIS**：以 PBRT 为主，明确积分测度、采样分布和估计量；这里的 PDF 是概率密度，不是文档格式。
- **阴影、SSAO、Bloom、IBL 等实际效果**：先用 LearnOpenGL 解释实现，再查 RTR4 说明适用条件、误差和性能成本。
- **GPU 对象与同步为什么需要显式管理**：用用户指定的 Khronos 教程讲解，再对照 Redflag 当前的 OpenGL 实现。不假装项目已有 Vulkan 后端。
- **引擎类名和封装方式**：仍以项目记忆中的 Hazel 源码约定为准。教材用于解释图形学，不自动替代现有工程规范。
- **资源层、Renderer 与 RHI 如何分工**：读《游戏引擎架构》的相关主题和 GAMES104 第 2 讲，再对照 Hazel 与 Piccolo。资产 ID 指向内容，RHI 对象表示 GPU 资源，帧内临时附件归渲染器管理。

## Redflag 的 RHI 学习与迁移路线

1. 用 GAMES101 的变换、光栅化和着色部分复核当前坐标与纹理约定。先把 NDC 深度、屏幕方向、矩阵布局和颜色空间写清楚。
2. 用 GAMES104 第 2 讲和《游戏引擎架构》检查模块依赖。结合 Hazel，把高层代码中的 OpenGL 类型、纹理编号和状态调用收进后端，维持现有 OpenGL 输出。
3. 用 Khronos 基础教程实现独立 Vulkan 验证程序，逐项验证设备能力、交换链、命令录制、资源上传和同步。SDK 安装成功不等于设备支持方案要求。
4. 读 Building a Simple Engine 的架构、资源与 GUI 章节，再接入 Redflag 的接口。当前工程是 C++17；教程的 Vulkan 1.4、C++20、Vulkan-Hpp RAII 和 Slang 是参考环境，不构成项目已采纳的依赖。
5. 用 GAMES202 对照阴影、SSAO/RSM 和时域效果，给每个 Pass 写明读写资源和历史失效条件，再迁移后处理与完整场景。以图像、拾取、Resize 和资源回收结果验收。

具体接口和阶段验收见 [RHI 与 Vulkan 接入设计](latex/rhi/main.tex)。当前实现仍见 [现有架构文档](latex/main.tex)；计划中的类型不表示已经落地。

## 讲解方式

1. 先说明这个概念解决什么问题，配一个能表达空间关系或数据流的图。
2. 列出变量含义、单位、定义域、坐标空间和前提条件。
3. 逐步推导关键等式，说明每一步为何成立，不直接堆出最终公式。
4. 给出一个简单数值例子，或用极端情况检查公式。
5. 对应到 Redflag 的实际变量、Shader 或函数，指出原理与工程近似的区别。
6. 引用确实查阅的章节、公式或图，区分教材结论和本次自行推导。

公式用 LaTeX。独立讲义和架构文档用 LaTeX，解释性示意图可用 TikZ。
不同教材的入射/出射方向、左右手系、矩阵乘法、NDC 深度范围、线性/显示空间、粗糙度参数化与概率密度测度，必须先统一或明确对照。

## Shader 注释的具体参考

本轮注释按“效果作用 → 直观步骤 → 变量与公式 → 输入输出”组织。先让读者知道这段着色器在画什么、为什么这样算，再解释坐标空间和实现细节。资源编号只在确实影响调用约定的位置注明，不以 slot/binding 清单开头。

| 效果 | 本轮核对的原文 | 注释需要讲清的内容 |
| --- | --- | --- |
| Bloom | [LearnOpenGL：Bloom](https://learnopengl.com/Advanced-Lighting/Bloom) | 亮部怎样扩散到周围，再与原图合成；Redflag 的多级降采样/上采样是项目实现，不能说成教程原样算法 |
| SSAO | [LearnOpenGL：SSAO](https://learnopengl.com/Advanced-Lighting/SSAO) | 从表面附近的样本估计环境遮挡；解释采样空间、深度比较和半径。项目按整个采样核大小归一化，落在屏幕外的样本贡献为零，边缘的遮挡可能减弱 |
| 阴影 | [LearnOpenGL：Shadow Mapping](https://learnopengl.com/Advanced-Lighting/Shadows/Shadow-Mapping) | 先记录光源看到的最近深度，再判断当前点是否被遮挡；解释投影转换与偏移的用途 |
| PBR | 上表的 [LearnOpenGL：PBR Theory](https://learnopengl.com/PBR/Theory) | 区分辐射量、能量约束与微表面模型，说明材质参数对反射的影响，不只罗列函数名 |
| Tonemap | Krzysztof Narkowicz，[ACES Filmic Tone Mapping Curve](https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/)，2016-01-06 | 曲线将高动态范围亮度压到显示范围；项目采用的系数属于作者给出的 ACES 拟合曲线，不等于实现了完整 ACES 色彩管理流程 |

上述链接用于解释原理和核对公式。项目代码中的近似、采样次数、分支和数据布局仍按实际实现说明，不复制原文长段，也不把项目改法冒称教材结论。

## 排错与技术选型的具体参考

| 主题 | 已核对入口 | 对应的项目问题 |
| --- | --- | --- |
| 资产身份 | [Unity 6.0：Asset metadata](https://docs.unity3d.com/6000.0/Documentation/Manual/AssetMetadata.html) | 区分资产 ID 与文件位置，移动时保留元数据。Redflag 当前只接通模型 ID，不能类比为完整 Unity 导入数据库 |
| 透明裁剪 | [LearnOpenGL：Blending](https://learnopengl.com/Advanced-OpenGL/Blending) | 区分丢弃像素与半透明混合，解释深度和颜色轮廓一致的前提 |
| 实例化 | [LearnOpenGL：Instancing](https://learnopengl.com/Advanced-OpenGL/Instancing) | 一次绘制多个相同网格实例；具体分组限制与性能仍以 Redflag 源码和测量为准 |
| RSM 通量与几何项 | Dachsbacher、Stamminger，[Reflective Shadow Maps，I3D 2005 原论文](https://users.soe.ucsc.edu/~pang/160/s13/proposal/mijallen/proposal/media/p203-dachsbacher.pdf)，[DOI](https://doi.org/10.1145/1053427.1053460)，§3.1–3.3、印刷页 204–205 | 已核对通量生成、距离因子与无间接遮挡假设。Redflag 另推导按投影面积存密度的等面积圆盘估计，不将该密度与论文的单纹素通量混用。原文只在本机阅读，不随项目再分发 |
| Lambert 系数 | [PBRT 第四版 §9.2：Diffuse Reflection](https://pbr-book.org/4ed/Reflection_Models/Diffuse_Reflection) | 核对漫反射 BRDF 中的反射率与 1/π，解释 RSM 发送和接收两端各自的系数 |
| GPU 对象释放 | [Khronos：glDeleteVertexArrays](https://registry.khronos.org/OpenGL-Refpages/gl4/html/glDeleteVertexArrays.xhtml) | VAO 的创建与删除必须配对，C++ 引用释放不自动删除未封装好的 GL 对象 |
| 深度读回 | [Khronos：glReadPixels](https://registry.khronos.org/OpenGL-Refpages/gl4/html/glReadPixels.xhtml) | 核对读取范围、格式与目标内存，区分 CPU 预览和 GPU 附件本身 |
| 截帧中的资源绑定 | [RenderDoc 官方文档：Pipeline State](https://github.com/baldurk/renderdoc/blob/v1.x/docs/window/pipeline_state.rst) | 在选定绘制之后核对实际状态、参数缓冲与纹理，不仅查看调用代码 |
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

## 补充入口

- RTR4 [参考文献](https://www.realtimerendering.com/refs.html)。
- RTR4 [线性代数与三角学附录](https://www.realtimerendering.com/Real-Time_Rendering_4th-Appendices.pdf)。
- RTR4 [实时光线追踪在线章节](https://www.realtimerendering.com/Real-Time_Rendering_4th-Real-Time_Ray_Tracing.pdf)。
- RTR4 [碰撞检测在线章节](https://www.realtimerendering.com/Real-Time_Rendering_4th-Collision_Detection.pdf)。

## 版本约定

- 引用纸书页码必须先确认版次；没有正文可查时，只给官方入口和能够核实的章节信息。
- PBRT 第三版和第四版的组织、符号及代码不同，不混用章号、公式编号和页码。
- 核对时 Khronos latest 教程采用 Vulkan 1.4、C++20、Vulkan-Hpp RAII，并使用 Slang。以后引用要重新核对，不直接把旧 C API 教程代码拼进该版本，也不因此擅自升级 Redflag 的 C++17 工程。
- Jason Gregory 作者站目前介绍第四版两卷本，新增或扩充 GPU 编程、渲染与光照、motion matching、C++23 等内容。第三版和第四版的章节组织不同；本项目尚未获得对应版次全文，本轮只据官方介绍和实际查阅的公开资料讨论，不引用未经核实的书中图号。
- 后续用户提供具体版本或本地书籍时，将文件位置与版次补入本目录。
