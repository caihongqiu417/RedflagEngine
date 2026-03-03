# Redflag 引擎技术白皮书：渲染管线与架构解析 (SPIR-V & UBO Update)

本文档旨在阐述 Redflag 引擎在引入 SPIR-V 和 Uniform Buffer 技术后的**现有渲染管线**与**整体架构**。

---

## 1. 整体架构概览 (Architecture Overview)

Redflag 引擎采用了**分层架构**，自下而上分为三层：

### 1.1 平台层 (Platform Layer)
*   **窗口与输入**：由 `GLFW` 管理窗口上下文和输入事件。
*   **图形 API 抽象**：`RendererAPI` 提供了统一的绘图接口（如 `DrawIndexed`）。目前的实现是 `OpenGLRendererAPI`。
*   **渲染上下文**：`GraphicsContext` 负责交换缓冲区（Swap Buffers）和上下文初始化。

### 1.2 资源层 (Resource Layer) - **核心更新区域**
这一层负责管理 GPU 资源，是本次更新的重灾区。
*   **Shader 系统**：
    *   **资产化**：Shader 不再是简单的代码字符串，而是被编译、反射、缓存后的**SPIR-V 二进制资产**。
    *   **反射机制**：通过 `SPIRV-Cross` 分析 Shader，C++ 层自动获知 Uniform 变量布局。
*   **Buffer 系统**：
    *   **VertexBuffer / IndexBuffer**：存储模型顶点数据。
    *   **UniformBuffer (UBO)**：**[新增]** 存储全局共享数据（如相机矩阵），实现一次更新、全 Shader 共享。
*   **Texture 系统**：纹理资源管理，支持多纹理槽位绑定。

### 1.3 渲染层 (Renderer Layer)
*   **Renderer2D**：面向用户的 2D 批处理渲染器。
    *   负责提交 Quad（四边形），自动合批（Batching）。
    *   管理渲染状态（如混合模式、深度测试）。
    *   **数据流向**：`Scene` -> `Renderer2D::BeginScene` -> `UniformBuffer Update` -> `Flush` -> `DrawCall`。

---

## 2. 渲染管线流程详解 (Rendering Pipeline)

以下是引擎每一帧从“开始”到“结束”的完整数据流向：

### 阶段一：初始化 (Initialization)

1.  **资源加载**：
    *   引擎启动时，`ShaderLibrary` 加载 `Texture.glsl`。
    *   **SPIR-V 编译**：检查缓存 -> (若无) 调用 `shaderc` 编译 -> 写入磁盘。
    *   **SPIR-V 反射**：分析出 `uniform Camera` Block 需要 64 字节，绑定点为 0。
2.  **UBO 创建**：
    *   `Renderer2D` 初始化时，创建一个 `UniformBuffer`，大小为 64 字节，绑定到 Binding Point 0。
    *   显存被分配，准备接收数据。

### 阶段二：场景提交 (Scene Submission)

1.  **开始场景 (`BeginScene`)**：
    *   接收相机（Camera）对象。
    *   计算 `ViewProjection` 矩阵。
    *   **更新 UBO**：调用 `s_Data.CameraUniformBuffer->SetData(&viewProj)`。
        *   此时，相机矩阵被写入显存中的 UBO 区域。
        *   注意：此时不需要绑定任何 Shader，因为 UBO 是全局绑定的。
2.  **提交图元 (`DrawQuad`)**：
    *   用户调用 `DrawQuad`。
    *   CPU 端将顶点数据写入 `QuadVertexBuffer` 的内存指针中（批处理）。
    *   如果达到批处理上限（如 10,000 个 Quad），触发 `FlushAndReset`。

### 阶段三：渲染执行 (Execution / Flush)

1.  **状态绑定**：
    *   绑定纹理到对应的纹理槽位（0-31）。
    *   绑定 Shader (`Texture.glsl`)。
2.  **数据读取 (GPU 端)**：
    *   当 Shader 执行 Vertex Shader 时，它声明了 `layout(binding = 0) uniform Camera`。
    *   GPU 自动从绑定点 0 的 UBO 中读取刚才写入的 `ViewProjection` 矩阵。
    *   GPU 从顶点属性中读取 Position、Color 等数据。
3.  **绘制命令**：
    *   调用 `glDrawElements`，GPU 开始光栅化。

---

## 3. Shader 编译全流程图解 (Shader Compilation Pipeline)

这是你最关心的部分：从你写下 `.glsl` 到最终 GPU 运行它，中间经历了什么？

1.  **输入**：`Texture.glsl` (源码字符串)。
2.  **预处理 (PreProcess)**：引擎将文件拆分为 `Vertex Source` 和 `Fragment Source`。
3.  **编译 (shaderc)**：
    *   输入：GLSL 源码。
    *   动作：调用 `shaderc::CompileGlslToSpv`。
    *   输出：**Vulkan SPIR-V** (通用二进制格式，类似 Java 的字节码)。
    *   *注：这是第一次编译，生成的二进制会被缓存到硬盘 `.vulkan.spv`。*
4.  **反射 (SPIRV-Cross)**：
    *   输入：Vulkan SPIR-V。
    *   动作：分析二进制，提取变量名、Binding Point、Buffer 大小。
    *   输出：反射数据 (`ShaderUniformBuffer` 结构体)。
5.  **转换 (SPIRV-Cross)**：
    *   输入：Vulkan SPIR-V。
    *   动作：**反编译**回 GLSL 源码。
    *   输出：**OpenGL 兼容 GLSL** (源码字符串)。
    *   *注：这一步是为了让代码适应 OpenGL 的特定方言。*
6.  **二次编译 (shaderc)**：
    *   输入：OpenGL 兼容 GLSL。
    *   动作：再次调用 `shaderc`。
    *   输出：**OpenGL SPIR-V** (最终二进制)。
    *   *注：这是第二次编译，生成的二进制也会被缓存 `.opengl.spv`。*
7.  **加载 (OpenGL Driver)**：
    *   输入：OpenGL SPIR-V。
    *   动作：调用 `glShaderBinary` 和 `glSpecializeShader`。
    *   输出：`GLuint ProgramID` (显卡可执行的程序句柄)。

---

## 4. 核心代码结构映射与解析

### 4.1 `Redflag/src/Platform/OpenGL/OpenGLShader.cpp`
**职责**：负责 Shader 资源的生命周期管理，包括编译、反射、缓存和加载。

*   **`CompileOrGetVulkanBinaries` (GLSL -> Vulkan SPIR-V)**：
    *   **输入**：原始 GLSL 源代码字符串。
    *   **过程**：调用 `shaderc` 编译器。我们开启了 `AutoMapLocations`，让编译器自动为没有 location 的变量分配位置。
    *   **输出**：生成符合 Vulkan 标准的 SPIR-V 二进制数据（`vector<uint32_t>`）。
    *   **作用**：作为中间格式，供后续反射和跨平台转换使用。这也是被缓存到硬盘的主要格式。

*   **`CompileOrGetOpenGLBinaries` (SPIR-V -> GLSL -> SPIR-V)**：
    *   **输入**：上一步生成的 Vulkan SPIR-V。
    *   **过程**：
        1.  使用 `SPIRV-Cross` 将 Vulkan SPIR-V **反编译** 回 GLSL 源码（为了兼容 OpenGL 驱动的特性）。
        2.  再次调用 `shaderc` 将这个新的 GLSL 编译为 OpenGL 专用的 SPIR-V。
    *   **作用**：生成最终能被 `glShaderBinary` 加载给 OpenGL 驱动的数据。

*   **`Reflect` (反射分析)**：
    *   **输入**：SPIR-V 二进制数据。
    *   **过程**：使用 `SPIRV-Cross` 遍历 Shader 中的所有资源。
    *   **输出**：
        *   “有一个叫 `Camera` 的 Uniform Block，大小 64 字节，绑定点 0”。
        *   “有一个叫 `u_Textures` 的采样器数组，绑定点 0”。
    *   **价值**：让 C++ 代码不再盲目，可以根据这些信息自动创建 UI 面板或检查资源匹配情况。

### 4.2 `Redflag/src/Platform/OpenGL/OpenGLUniformBuffer.cpp`
**职责**：封装 OpenGL 的 UBO 操作，提供从 CPU 向 GPU 特定显存区域传输数据的通道。

*   **`glNamedBufferData` (显存分配)**：
    *   **类比**：`malloc`。在显卡内存中申请一块指定大小（如 64 字节）的空间。
    *   **参数**：`GL_DYNAMIC_DRAW` 表示这块内存我们会频繁修改（每一帧都改）。

*   **`glBindBufferBase` (绑定到槽位)**：
    *   **类比**：插头插到插座上。将刚才申请的这块显存，连接到 OpenGL 上下文的“Binding Point 0”插座上。
    *   **重要性**：Shader 里写了 `binding = 0`，它就去 0 号插座找数据。如果我们不插上去，Shader 就读不到。

*   **`glNamedBufferSubData` (数据传输)**：
    *   **类比**：`memcpy`。将 CPU 内存中的 `viewProj` 矩阵复制到刚才申请的显存中。
    *   **特点**：直接操作显存，无需绑定 Buffer（DSA - Direct State Access 技术），效率更高。

### 4.3 `Redflag/src/Redflag/Renderer/Renderer2D.cpp`
**职责**：渲染管线的指挥官。负责调度资源，将场景数据组装成 GPU 能理解的命令。

*   **`Init` (初始化)**：
    *   调用 `UniformBuffer::Create(64, 0)`。
    *   **意义**：在游戏启动时，就把存放相机数据的“仓库”建好，并挂在 0 号钩子上。

*   **`BeginScene` (每帧开始)**：
    *   调用 `CameraUniformBuffer->SetData(...)`。
    *   **意义**：每帧开始画图前，先把摄像机摆好（更新矩阵数据到仓库）。注意这里**不需要**绑定任何 Shader，因为仓库是全局共享的。

*   **`Flush` (提交绘制)**：
    *   `Shader->Bind()`：告诉 GPU，“用这个 Shader 程序来画”。
    *   `Texture->Bind()`：告诉 GPU，“用这张图片”。
    *   `RenderCommand::DrawIndexed()`：告诉 GPU，“开始画吧”。
    *   **关联**：当 GPU 开始画时，它会自动去 0 号钩子（UBO）拿相机数据，去纹理槽位拿图片，结合顶点数据，最终画出像素。

---

## 5. 为什么这个架构更好？

### 对比旧架构

| 特性 | 旧架构 (Legacy) | 新架构 (Modern) |
| :--- | :--- | :--- |
| **Shader 编译** | 运行时源码编译，慢，无缓存 | 离线/运行时 SPIR-V 编译，支持二进制缓存，秒开 |
| **全局数据 (相机)** | 每个 Shader 单独上传 (`SetMat4`) | **Uniform Buffer** 全局共享，一次上传，全员可见 |
| **Shader 变量** | 需要人工核对变量名字符串 | **反射系统** 自动提取变量名和布局 |
| **扩展性** | 仅限 OpenGL | 兼容 Vulkan/DX12 (通过 SPIR-V) |

### 总结
目前的架构已经从“简单的 OpenGL 包装器”进化为**数据驱动的现代渲染引擎**。
*   **数据驱动**：Shader 决定数据布局，C++ 自动适应。
*   **资源分离**：Shader、Buffer、Texture 各司其职，通过 Binding Point 动态链接，解耦了 Shader 和渲染逻辑。

---

## 6. 实战指南：如何添加一个新的 Uniform 变量？

这部分解答了你关于“反射是否实现”以及“如何加新变量”的疑问。

### 6.1 反射实现了吗？(Has Reflection?)
**是的，实现了**。
*   **证据**：在 `OpenGLShader::Reflect` 函数中，代码已经能够遍历 SPIR-V，并打印出所有 Uniform Buffer 和 Resource 的名称、大小和绑定点。
*   **局限**：目前的反射数据虽然被提取到了 `m_ShaderUniformBuffers` 结构体中，但**材质系统 (Material System)** 还没有实现。这意味着引擎虽然“知道”有哪些变量，但还没有提供一个自动化的 UI 面板来让你操作它们。

### 6.2 如何添加一个新的 Uniform？

假设你要加一个 `u_Time` 变量来实现动态效果。

#### 场景 A：添加全局变量 (适合所有物体都共用的，如时间、风力)
1.  **修改 Shader (`Texture.glsl`)**：
    在 `Camera` Block 后面，再加一个 Block（或者加在 Camera Block 里，如果它和相机相关）。
    ```glsl
    // 绑定到 Binding Point 1
    layout(std140, binding = 1) uniform SceneData
    {
        float u_Time;
        float u_Padding[3]; // 填充到 16 字节对齐
    };
    ```
2.  **修改 C++ (`Renderer2D.cpp`)**：
    *   `Init`: 创建一个新的 UBO。
        ```cpp
        s_Data.SceneUniformBuffer = UniformBuffer::Create(16, 1); // 大小 16，Binding 1
        ```
    *   `BeginScene`: 更新时间数据。
        ```cpp
        float time = glfwGetTime();
        s_Data.SceneUniformBuffer->SetData(&time, sizeof(float));
        ```
    *   **完成！** 此时所有 Shader 都能读到 `u_Time`。

#### 场景 B：添加材质参数 (适合每个物体不同的，如金属度、颜色)
*注意：由于还没实现材质系统，目前只能用旧方法（SetUniform）或者 Push Constant（未来特性）。这里介绍兼容做法。*

1.  **修改 Shader**：
    使用 Push Constant 或者普通的 Uniform (如果不兼容 Vulkan)。
    ```glsl
    // 临时方案：用普通 Uniform
    uniform float u_Metallic;
    ```
2.  **修改 C++**：
    在 `Renderer2D::DrawQuad` 或 `Flush` 时，手动设置。
    ```cpp
    s_Data.TextureShader->SetFloat("u_Metallic", 0.5f);
    ```
    *注：这就是为什么我们还要保留 `SetInt/SetFloat` 接口的原因——为了灵活性。*
