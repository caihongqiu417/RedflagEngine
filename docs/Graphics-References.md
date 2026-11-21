# 图形学参考资料

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

## 按问题选教材

- **矩阵、坐标变换、投影为什么这样写**：先用 FCG 建立几何直觉，再用 LearnOpenGL 对照实际代码。
- **光照公式、余弦项、立体角、BRDF 的来源**：以 PBRT 的定义和推导为主，再说明 RTR4 与 LearnOpenGL 中的实时近似。
- **采样、PDF、蒙特卡洛、重要性采样、MIS**：以 PBRT 为主，明确积分测度、采样分布和估计量；这里的 PDF 是概率密度，不是文档格式。
- **阴影、SSAO、Bloom、IBL 等实际效果**：先用 LearnOpenGL 解释实现，再查 RTR4 说明适用条件、误差和性能成本。
- **GPU 对象与同步为什么需要显式管理**：用用户指定的 Khronos 教程讲解，再对照 Redflag 当前的 OpenGL 实现；不假装项目已有 Vulkan 后端。
- **引擎类名和封装方式**：仍以项目记忆中的 Hazel 源码约定为准。教材用于解释图形学，不自动替代现有工程规范。

## 讲解方式

1. 先说明这个概念解决什么问题，配一个能表达空间关系或数据流的图。
2. 列出变量含义、单位、定义域、坐标空间和前提条件。
3. 逐步推导关键等式，说明每一步为何成立，不直接堆出最终公式。
4. 给出一个简单数值例子，或用极端情况检查公式。
5. 对应到 Redflag 的实际变量、Shader 或函数，指出原理与工程近似的区别。
6. 引用确实查阅的章节、公式或图，区分教材结论和本次自行推导。

公式用 LaTeX。独立讲义和架构文档用 LaTeX，解释性示意图可用 TikZ。
不同教材的入射/出射方向、左右手系、矩阵乘法、NDC 深度范围、线性/显示空间、粗糙度参数化与概率密度测度，必须先统一或明确对照。

## 图像来源

- LearnOpenGL：优先查对应章节内的原图、图注和第三方署名。
- PBRT：查对应版次的在线章节图和图注，引用具体章节 URL。
- RTR4：[官方图片库](https://www.realtimerendering.com/figures.html)，引用时保留来源，并依据书中图注保留原作者署名。
- FCG：查作者配套网站和实际使用版次；未取得原图时可自行画解释图，不能编造书中图号。
- Vulkan：查 Khronos 教程对应页的原图和署名，不把其他版本的截图当作当前教程。

每次引用都记录：**教材/网站、版次、章节、图号或页码（能核实才填）、原始链接、原图或重绘**。
实际使用时核对图像的授权条件；重绘写“据……重绘”，原创写“示意图”。引用来源不等于把原书整章或整本复制进资料库。

## 补充入口

- RTR4 [参考文献](https://www.realtimerendering.com/refs.html)。
- RTR4 [线性代数与三角学附录](https://www.realtimerendering.com/Real-Time_Rendering_4th-Appendices.pdf)。
- RTR4 [实时光线追踪在线章节](https://www.realtimerendering.com/Real-Time_Rendering_4th-Real-Time_Ray_Tracing.pdf)。
- RTR4 [碰撞检测在线章节](https://www.realtimerendering.com/Real-Time_Rendering_4th-Collision_Detection.pdf)。

## 版本约定

- 引用纸书页码必须先确认版次；没有正文可查时，只给官方入口和能够核实的章节信息。
- PBRT 第三版和第四版的组织、符号及代码不同，不混用章号、公式编号和页码。
- 核对时 Khronos latest 教程采用 Vulkan 1.4、C++20、Vulkan-Hpp RAII，并使用 Slang。以后引用要重新核对，不直接把旧 C API 教程代码拼进该版本，也不因此擅自升级 Redflag 的 C++17 工程。
- 后续用户提供具体版本或本地书籍时，将文件位置与版次补入本目录。
