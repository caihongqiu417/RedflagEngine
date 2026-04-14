# Redflag 引擎渲染管线完整文档

> **版本**: 2026-04-14  
> **引擎**: Redflag (Hazel-like 自研游戏引擎)  
> **图形 API**: OpenGL 4.5 + SPIR-V  
> **着色语言**: GLSL 450 → shaderc → spirv_cross → glShaderBinary  
> **构建**: Premake5 → VS2022, C++17

---

## 目录

1. [架构总览](#1-架构总览)
2. [初始化流程](#2-初始化流程)
3. [帧循环总流程](#3-帧循环总流程)
4. [SceneRenderer — 场景渲染调度器](#4-scenerenderer--场景渲染调度器)
5. [Renderer2D — 2D 批处理渲染器](#5-renderer2d--2d-批处理渲染器)
6. [Renderer3D — 3D PBR 渲染器](#6-renderer3d--3d-pbr-渲染器)
7. [灯光系统与 UBO 布局](#7-灯光系统与-ubo-布局)
8. [PBR 着色器详解](#8-pbr-着色器详解)
9. [材质系统](#9-材质系统)
10. [模型加载 (Assimp)](#10-模型加载-assimp)
11. [Shader 编译管线 (SPIR-V)](#11-shader-编译管线-spir-v)
12. [帧缓冲与 Entity ID 拾取](#12-帧缓冲与-entity-id-拾取)
13. [编辑器覆盖层 (Gizmo)](#13-编辑器覆盖层-gizmo)
14. [ECS 组件一览](#14-ecs-组件一览)
15. [附录：std140 对齐规则备忘](#15-附录std140-对齐规则备忘)

---

## 1. 架构总览

```
┌───────────────────────────────────────────────────────────────┐
│  ForgeLayer (编辑器层 — 入口)                                  │
│  ┌─────────────────────────────────┐                          │
│  │ OnUpdate(ts)                    │                          │
│  │  ├─ 绑定 Framebuffer           │                          │
│  │  ├─ 清屏 + 清除 EntityID 附件   │                          │
│  │  ├─ Scene::OnUpdateEditor()     │ ← 或 OnUpdateRuntime()  │
│  │  │    └─ SceneRenderer ─────────┼──────────────────┐       │
│  │  │         ├─ Renderer2D        │   2D 批处理       │       │
│  │  │         ├─ CollectLights     │   灯光 UBO       │       │
│  │  │         └─ Renderer3D        │   3D PBR         │       │
│  │  ├─ 编辑器覆盖层 (Gizmo)       │                          │
│  │  ├─ Entity ID 拾取              │                          │
│  │  └─ 解绑 Framebuffer           │                          │
│  └─────────────────────────────────┘                          │
│                                                               │
│  OnImGuiRender()                                              │
│  ├─ Viewport 面板 (显示 FBO 颜色附件)                         │
│  ├─ SceneHierarchy 面板                                       │
│  ├─ Properties 面板                                           │
│  └─ ContentBrowser 面板                                       │
└───────────────────────────────────────────────────────────────┘
```

**核心类关系图：**

```
Renderer (总入口)
 ├─ RenderCommand        OpenGL 状态控制 (clear/viewport/drawcall)
 ├─ Renderer2D           2D 批处理 (Quad/Circle/Line)
 ├─ Renderer3D           3D 命令队列 (Mesh/Model + 材质排序)
 └─ SceneRenderer        场景编排层 (ECS 遍历 → Renderer2D/3D 调用)

Scene (数据层)
 ├─ entt::registry       ECS 实体注册表
 ├─ Components.h         所有组件类型定义
 └─ Box2D PhysicsWorld   2D 物理世界

GPU 资源
 ├─ UniformBuffer        UBO (Camera / Transform / Material / Lights)
 ├─ Shader               SPIR-V 编译 + 缓存
 ├─ Texture2D            stb_image 加载
 ├─ Framebuffer          多附件 FBO (Color + EntityID + Depth)
 ├─ VertexArray/Buffer   VAO/VBO/IBO
 ├─ Material             PBR Metallic-Roughness 材质
 ├─ Mesh                 单个网格 (顶点+索引)
 └─ Model                Assimp 多子网格模型
```

---

## 2. 初始化流程

引擎启动时，`Renderer::Init()` 按顺序初始化所有子系统：

```cpp
// Renderer.cpp
void Renderer::Init()
{
    RenderCommand::Init();     // 1. OpenGL 全局状态
    Renderer2D::Init();        // 2. 2D 批处理资源
    Renderer3D::Init();        // 3. 3D PBR 资源
    SceneRenderer::Init();     // 4. 灯光 UBO
}
```

### 2.1 RenderCommand::Init — OpenGL 全局状态

```cpp
// OpenGLRendererAPI.cpp
void OpenGLRendererAPI::Init()
{
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
}
```

- **混合模式**：标准 Alpha 混合，源颜色 × srcAlpha + 目标颜色 × (1-srcAlpha)
- **深度测试**：默认开启，确保前后遮挡正确

### 2.2 Renderer2D::Init — 2D 批处理资源

分配 CPU 端顶点缓冲、创建 GPU VAO/VBO/IBO、加载着色器：

| 资源 | 说明 |
|------|------|
| `QuadVertexArray` | 四边形 VAO，包含 VBO + IBO |
| `QuadVertexBuffer` | 动态 VBO，容量 = 10000 × 4 顶点 × sizeof(QuadVertex) |
| `QuadVertexBufferBase` | CPU 端 `new QuadVertex[40000]`，每帧填写后上传 GPU |
| `CircleVertexArray/Buffer` | 圆形，与 Quad 共享 IBO |
| `LineVertexArray/Buffer` | 线段，`GL_LINES` 绘制 |
| `WhiteTexture` | 1×1 纯白纹理，纯色绘制时绑定此纹理 |
| `QuadShader` | `Renderer2D_Quad.glsl` |
| `CircleShader` | `Renderer2D_Circle.glsl` |
| `LineShader` | `Line.glsl` |
| `CameraUniformBuffer` | binding=0, 大小 64 字节 (mat4) |

```cpp
// 索引缓冲预生成：每个四边形 6 个索引，两个三角形
// 顶点顺序: 0-1-2, 2-3-0
for (uint32_t i = 0; i < MaxQuads; i++)
{
    quadIndices[i*6+0] = i*4+0;  quadIndices[i*6+1] = i*4+1;
    quadIndices[i*6+2] = i*4+2;  quadIndices[i*6+3] = i*4+2;
    quadIndices[i*6+4] = i*4+3;  quadIndices[i*6+5] = i*4+0;
}
```

### 2.3 Renderer3D::Init — 3D PBR 资源

```cpp
// Renderer3D.cpp
void Renderer3D::Init()
{
    s_Data.MeshShader = Shader::Create("assets/shaders/Renderer3D_Mesh.glsl");

    // 1×1 白色纹理，无贴图材质的默认绑定
    s_Data.WhiteTexture = Texture2D::Create(1, 1);
    uint32_t whiteData = 0xffffffff;
    s_Data.WhiteTexture->SetData(&whiteData, sizeof(uint32_t));

    // 默认白色材质
    s_Data.DefaultMaterial = Material::Create(glm::vec4(1.0f));

    // 三个 UBO：Camera(0), Transform(1), Material(2)
    s_Data.CameraUniformBuffer   = UniformBuffer::Create(sizeof(CameraData), 0);    // 80 bytes
    s_Data.TransformUniformBuffer = UniformBuffer::Create(sizeof(TransformData), 1); // 64 bytes
    s_Data.MaterialUniformBuffer  = UniformBuffer::Create(sizeof(MaterialData), 2);  // 48 bytes

    s_Data.DrawCommands.reserve(1024);
}
```

### 2.4 SceneRenderer::Init — 灯光 UBO

```cpp
// SceneRenderer.cpp
static constexpr uint32_t LIGHT_UBO_SIZE =
    sizeof(GPULightUBO) +  // 16 bytes (header)
    (MAX_DIRECTIONAL_LIGHTS + MAX_POINT_LIGHTS) * sizeof(GPULightData);  // 17 × 64 bytes

void SceneRenderer::Init()
{
    s_LightUBO = UniformBuffer::Create(LIGHT_UBO_SIZE, LIGHT_UBO_BINDING);  // binding = 3
}
```

---

## 3. 帧循环总流程

每帧由 `ForgeLayer::OnUpdate(Timestep ts)` 驱动：

```
ForgeLayer::OnUpdate(ts)
│
├── 1. 更新相机 (仅 Viewport 聚焦时)
│   ├── m_CameraController.OnUpdate(ts)   // 正交相机
│   └── m_EditorCamera.OnUpdate(ts)       // 编辑器透视相机
│
├── 2. 重置统计 & 绑定 FBO
│   ├── Renderer2D::ResetStats()
│   ├── m_Framebuffer->Bind()
│   ├── RenderCommand::SetClearColor({0.1, 0.1, 0.1, 1.0})
│   ├── RenderCommand::Clear()
│   └── m_Framebuffer->ClearAttachment(1, -1)  // EntityID 附件清为 -1
│
├── 3. 按场景状态分发
│   ├── SceneState::Edit
│   │   ├── Scene::OnUpdateEditor(ts, camera)
│   │   │   └── SceneRenderer::RenderScene(scene, camera)   ← 核心
│   │   └── 编辑器覆盖层 (Gizmo、碰撞体线框)
│   │
│   ├── SceneState::Play
│   │   └── Scene::OnUpdateRuntime(ts)
│   │       ├── 执行 NativeScript
│   │       ├── Box2D 物理步进
│   │       ├── 查找主相机
│   │       └── SceneRenderer::RenderRuntime(scene, proj, camTransform)
│   │
│   └── SceneState::Simulate
│       └── Scene::OnUpdateSimulation(ts, camera)
│           ├── Box2D 物理步进 (不执行脚本)
│           └── SceneRenderer::RenderScene(scene, camera)
│
├── 4. Entity ID 拾取 (读取 RED_INTEGER 附件)
│
├── 5. 解绑 FBO
│   └── m_Framebuffer->Unbind()
│
└── 6. OnImGuiRender() — UI 面板绘制
```

---

## 4. SceneRenderer — 场景渲染调度器

**设计思想**：将「怎么画」从 `Scene` 中剥离。Scene 只持有数据（ECS registry），SceneRenderer 负责遍历实体、收集灯光、调度 Renderer2D/3D。

### 4.1 编辑器模式 RenderScene

```cpp
void SceneRenderer::RenderScene(Scene& scene, const EditorCamera& camera)
{
    entt::registry& registry = scene.GetRegistry();

    // ========== 2D 渲染 ==========
    Renderer2D::BeginScene(camera);
    {
        // 所有 Sprite 实体
        auto view = registry.view<TransformComponent, SpriteRendererComponent>();
        view.each([&](auto entity, TransformComponent& tc, SpriteRendererComponent& sprite) {
            Renderer2D::DrawSprite(tc.GetTransform(), sprite, (int)entity);
        });
    }
    {
        // 所有 Circle 实体
        auto view = registry.view<TransformComponent, CircleRendererComponent>();
        view.each([&](auto entity, TransformComponent& tc, CircleRendererComponent& circle) {
            Renderer2D::DrawCircle(tc.GetTransform(), circle.Color, circle.Thickness, circle.Fade, (int)entity);
        });
    }
    Renderer2D::EndScene();

    // ========== 灯光收集 ==========
    CollectAndUploadLights(scene);

    // ========== 3D 渲染 ==========
    Renderer3D::BeginScene(camera);
    {
        // MeshRendererComponent → 单个 Mesh
        auto view = registry.view<TransformComponent, MeshRendererComponent>();
        view.each([&](auto entity, TransformComponent& tc, MeshRendererComponent& mr) {
            if (mr.MeshData) {
                if (mr.Mat)          Renderer3D::DrawMesh(mr.MeshData, tc.GetTransform(), mr.Mat, (int)entity);
                else if (mr.Texture) Renderer3D::DrawMesh(mr.MeshData, tc.GetTransform(), mr.Texture, (int)entity);
                else                 Renderer3D::DrawMesh(mr.MeshData, tc.GetTransform(), mr.Color, (int)entity);
            }
        });
    }
    {
        // ModelRendererComponent → 多子网格模型（Assimp）
        auto view = registry.view<TransformComponent, ModelRendererComponent>();
        view.each([&](auto entity, TransformComponent& tc, ModelRendererComponent& mr) {
            if (!mr.ModelAsset && !mr.FilePath.empty())
                mr.ModelAsset = AssetManager::GetOrLoad<Model>(mr.FilePath);  // 懒加载
            if (mr.ModelAsset)
                Renderer3D::DrawModel(mr.ModelAsset, tc.GetTransform(), mr.OverrideMaterial, mr.Color, (int)entity);
        });
    }
    Renderer3D::EndScene();
}
```

**完整每帧渲染管线序列图：**

```
ForgeLayer         Scene         SceneRenderer       Renderer2D       Renderer3D        GPU
    │                │                │                  │                │               │
    │──OnUpdate()──>│                │                  │                │               │
    │  Bind FBO     │                │                  │                │               │
    │  Clear        │                │                  │                │               │
    │               │                │                  │                │               │
    │──OnUpdateEditor()─>│           │                  │                │               │
    │               │──RenderScene()─>│                 │                │               │
    │               │                │──BeginScene()──>│                │               │
    │               │                │  遍历Sprite/Circle entities      │               │
    │               │                │──DrawSprite()──>│                │               │
    │               │                │──DrawCircle()──>│                │               │
    │               │                │──EndScene()───>│                │               │
    │               │                │                  │──Flush()────>│──glDrawElements
    │               │                │                  │                │               │
    │               │                │──CollectAndUploadLights()────────────────>│ UBO(3)
    │               │                │                  │                │               │
    │               │                │──BeginScene()────────────────>│               │
    │               │                │  遍历Mesh/Model entities      │               │
    │               │                │──DrawMesh()──────────────>│  push cmd     │
    │               │                │──DrawModel()─────────────>│  push cmds    │
    │               │                │──EndScene()──────────────>│               │
    │               │                │                  │  sort by material      │
    │               │                │                  │  for each cmd:         │
    │               │                │                  │    UploadMaterial→UBO(2)│
    │               │                │                  │    UploadTransform→UBO(1)
    │               │                │                  │    DrawIndexed──>│──glDrawElements
    │               │                │                  │                │               │
    │  编辑器 Gizmo  │                │                  │                │               │
    │──DrawLine()──>│                │                  │                │               │
    │  Unbind FBO   │                │                  │                │               │
```

---

## 5. Renderer2D — 2D 批处理渲染器

### 5.1 核心思想

将大量 2D 图元（四边形、圆、线段）合并到一个大的 VBO 中，一次 DrawCall 绘制所有图元，大幅减少 CPU→GPU 的状态切换开销。

### 5.2 数据结构

**QuadVertex（每个四边形 4 个顶点）：**
```cpp
struct QuadVertex {
    glm::vec3 Position;     // 世界空间位置（CPU 端已做矩阵变换）
    glm::vec4 Color;        // 顶点颜色
    glm::vec2 TexCoord;     // UV 坐标
    float     textureIndex; // 纹理数组索引 (0-31)
    float     TilingFactor; // 纹理平铺因子
    int       EntityID;     // 实体 ID（拾取用）
};
```

**CircleVertex：**
```cpp
struct CircleVertex {
    glm::vec3 WorldPosition;  // 世界空间位置
    glm::vec3 LocalPosition;  // 局部空间 (-1~1)，片元着色器用来计算距离
    float     Thickness;      // 圆环厚度 (1.0 = 实心)
    glm::vec4 Color;
    float     Fade;           // 边缘羽化
    int       EntityID;
};
```

### 5.3 纹理批处理

Renderer2D 维护一个最多 32 个纹理的数组。Slot 0 固定绑定纯白纹理（纯色绘制时使用）。

```
TextureSlots[0] = WhiteTexture   ← 永远占用
TextureSlots[1] = texture_A
TextureSlots[2] = texture_B
...
TextureSlots[31] = texture_Z
```

每个四边形的 `textureIndex` 记录它使用哪个 slot。如果某纹理已在数组中，直接复用；否则追加到数组末尾。当 slot 用尽或索引数量达上限时，触发 `FlushAndReset()` —— 先提交当前批次，再开始新一批。

### 5.4 BeginScene / EndScene 流程

```
BeginScene(camera)
│  上传 ViewProjection 到 Camera UBO (binding=0)
│  重置 QuadIndexCount、CircleIndexCount、LineVertexCount 为 0
│  重置写指针到缓冲区头部
│  重置 TextureSlotIndex = 1

DrawQuad() / DrawSprite() / DrawCircle() / DrawLine()
│  在 CPU 端 QuadVertexBufferPtr 处写入顶点数据
│  QuadIndexCount += 6（每个四边形 6 个索引）

EndScene()
│  Quad 批次：
│    上传 CPU 缓冲 → VBO (glBufferSubData)
│    绑定 QuadShader + 所有使用的纹理
│    glDrawElements(GL_TRIANGLES, QuadIndexCount)
│
│  Circle 批次：
│    上传 → VBO
│    绑定 CircleShader
│    glDrawElements(GL_TRIANGLES, CircleIndexCount)
│
│  Line 批次：
│    上传 → VBO
│    绑定 LineShader
│    glDrawArrays(GL_LINES, 0, LineVertexCount)
```

### 5.5 2D 着色器

**Renderer2D_Quad.glsl：**
- 顶点着色器：直接使用 CPU 端预变换好的 `a_Position`，乘以 UBO 中的 `u_ViewProjection`
- 片元着色器：根据 `v_TexIndex` 采样对应纹理（switch-case 展开 32 个分支），乘以顶点颜色
- 输出：`o_Color` (RGBA) + `o_EntityID` (int)

**Renderer2D_Circle.glsl：**
- 在局部坐标系 (-1~1) 中计算到圆心距离 `dist = length(v_LocalPos)`
- 用 `smoothstep` 实现内外边缘的平滑羽化
- `Thickness <= 0` 时绘制实心圆

**Line.glsl：**
- 最简单的着色器：顶点变换 + 顶点颜色透传
- `o_EntityID = -1`（线段不参与拾取）

---

## 6. Renderer3D — 3D PBR 渲染器

### 6.1 命令队列架构

Renderer3D 不立即绘制，而是将所有绘制请求收集到命令队列，在 `EndScene()` 中统一**排序后执行**。

```cpp
struct DrawCommand {
    Ref<Mesh>     MeshData;    // 网格数据
    glm::mat4     Transform;   // 最终世界变换
    Ref<Material> Mat;         // PBR 材质（可为 nullptr → 使用默认白色）
    int           EntityID;    // 实体 ID
};
```

### 6.2 材质排序

```cpp
// EndScene() 中按材质指针排序，相同材质的 DrawCommand 聚在一起
std::sort(DrawCommands.begin(), DrawCommands.end(),
    [](const DrawCommand& a, const DrawCommand& b) {
        return a.Mat->GetSortKey() < b.Mat->GetSortKey();
    });
```

`GetSortKey()` 返回 `BaseColorMap` 纹理指针的整数值。这样相同材质的物体连续绘制，减少纹理绑定和 UBO 上传的次数。

### 6.3 绘制执行流程

```cpp
void Renderer3D::EndScene()
{
    // 1. 排序
    std::sort(...);

    // 2. 绑定 PBR shader
    s_Data.MeshShader->Bind();

    // 3. 遍历排序后的命令
    uintptr_t lastKey = ~0ull;
    for (const auto& cmd : DrawCommands)
    {
        const Ref<Material>& mat = cmd.Mat ? cmd.Mat : DefaultMaterial;
        uintptr_t key = mat->GetSortKey();

        // 材质变化时才重新上传
        if (key != lastKey) {
            UploadMaterial(mat);   // → Material UBO (binding=2) + 贴图绑定
            lastKey = key;
        }

        // 每次必须更新 Transform
        TransformUniformBuffer->SetData(&cmd.Transform, ...);  // UBO binding=1
        MaterialBuffer.EntityID = cmd.EntityID;
        MaterialUniformBuffer->SetData(&MaterialBuffer, ...);   // UBO binding=2

        cmd.MeshData->Bind();
        RenderCommand::DrawIndexed(cmd.MeshData->GetVertexArray(), cmd.MeshData->GetIndexCount());
    }
}
```

### 6.4 UBO 布局（4 个 binding 点）

| Binding | 名称 | 大小 | 更新频率 | 内容 |
|---------|------|------|----------|------|
| 0 | Camera | 80 B | 每帧 1 次 | `mat4 ViewProjection` + `vec4 ViewPosition` |
| 1 | Transform | 64 B | 每次 DrawCall | `mat4 Transform` (模型矩阵) |
| 2 | Material | 48 B | 每次材质切换 | PBR 参数 + 贴图开关 + EntityID |
| 3 | Lights | 1104 B | 每帧 1 次 | 灯光数 + 方向光 × 1 + 点光 × 16 |

### 6.5 UploadMaterial 详解

```cpp
static void UploadMaterial(const Ref<Material>& mat)
{
    // 填充 CPU 端 MaterialBuffer
    MaterialBuffer.BaseColor  = mat->BaseColor;       // vec4, offset 0
    MaterialBuffer.Metallic   = mat->Metallic;         // float, offset 16
    MaterialBuffer.Roughness  = mat->Roughness;        // float, offset 20
    MaterialBuffer.AO         = mat->AO;               // float, offset 24
    MaterialBuffer.UseBaseColorMap        = mat->HasBaseColorMap()        ? 1 : 0;  // offset 28
    MaterialBuffer.UseNormalMap            = mat->HasNormalMap()            ? 1 : 0;  // offset 32
    MaterialBuffer.UseMetallicRoughnessMap = mat->HasMetallicRoughnessMap() ? 1 : 0;  // offset 36
    MaterialBuffer.UseAOMap               = mat->HasAOMap()                ? 1 : 0;  // offset 40
    // EntityID 在外部赋值                                                    // offset 44

    // 绑定 PBR 纹理到约定槽位
    if (mat->HasBaseColorMap())        mat->BaseColorMap->Bind(3);        // slot 3
    if (mat->HasNormalMap())           mat->NormalMap->Bind(4);           // slot 4
    if (mat->HasMetallicRoughnessMap()) mat->MetallicRoughnessMap->Bind(5); // slot 5
    if (mat->HasAOMap())               mat->AOMap->Bind(6);              // slot 6
}
```

### 6.6 DrawModel — 多子网格提交

```cpp
void Renderer3D::DrawModel(const Ref<Model>& model, const glm::mat4& transform,
                            const Ref<Material>& overrideMaterial, const glm::vec4& fallbackColor, int entityID)
{
    auto fallback = Material::Create(fallbackColor);

    for (const auto& subMesh : model->GetSubMeshes())
    {
        if (!subMesh.MeshData) continue;

        // 最终变换 = 实体的世界变换 × 子网格在模型内的局部变换
        glm::mat4 finalTransform = transform * subMesh.LocalTransform;

        // 材质优先级：覆盖材质 > 子网格自带材质 > 纯色回退
        Ref<Material> mat = overrideMaterial ? overrideMaterial
                          : (subMesh.Mat ? subMesh.Mat : fallback);

        DrawCommands.push_back({ subMesh.MeshData, finalTransform, mat, entityID });
    }
}
```

---

## 7. 灯光系统与 UBO 布局

### 7.1 C++ 数据结构

```cpp
// 每个灯光 64 字节
struct GPULightData {
    glm::vec4 Direction;     // xyz=方向, w=类型 (0=方向光, 1=点光)
    glm::vec4 Color;         // xyz=RGB,  w=强度
    glm::vec4 Position;      // xyz=位置, w=半径(点光)
    int       CastShadow;    // 0 或 1
    int       _pad[3];       // 填充到 64 字节（16 字节对齐）
};  // sizeof = 64

// UBO 头部 16 字节
struct GPULightUBO {
    int NumDirectionalLights;  // offset 0
    int NumPointLights;        // offset 4
    int _pad[2];               // offset 8, 填充到 16 字节
};  // sizeof = 16
```

### 7.2 UBO 内存布局

```
Offset    Size    内容
──────    ────    ──────────────────────────────────
0         16 B    GPULightUBO header
                    [0-3]   NumDirectionalLights (int)
                    [4-7]   NumPointLights (int)
                    [8-15]  padding
16        64 B    DirLights[0] (GPULightData)
                    [16-31]  Direction (vec4)
                    [32-47]  Color     (vec4)
                    [48-63]  Position  (vec4)
                    [64-67]  CastShadow (int)
                    [68-79]  _pad[3]
80        1024 B  PointLights[0..15] (16 × GPULightData)
                    [80-143]   PointLights[0]
                    [144-207]  PointLights[1]
                    ...
                    [1040-1103] PointLights[15]
──────    ────
Total     1104 B
```

### 7.3 GLSL 侧声明

```glsl
struct LightData {
    vec4 Direction;
    vec4 Color;
    vec4 Position;
    int  CastShadow;
    int  _pad0;      // ⚠️ 不能用 int _pad[3]！
    int  _pad1;      //    std140 数组每个元素对齐到 vec4(16字节)
    int  _pad2;      //    用独立 int 才与 C++ 布局匹配
};

layout(std140, binding = 3) uniform Lights {
    int       u_NumDirectionalLights;
    int       u_NumPointLights;
    int       _lightPad0;    // ⚠️ 同理：不能用 int _lightPad[2]
    int       _lightPad1;
    LightData u_DirLights[1];
    LightData u_PointLights[16];
};
```

> **关键 Bug 备忘**：GLSL std140 规则中，`int arr[N]` 数组的每个元素会被 pad 到 16 字节（vec4 对齐），但 C++ `int arr[N]` 每个元素只占 4 字节。必须用独立的 `int _pad0; int _pad1; int _pad2;` 代替 `int _pad[3]`，否则 GPU 与 CPU 的内存布局错位。详见 [附录](#15-附录std140-对齐规则备忘)。

### 7.4 CollectAndUploadLights

```cpp
void SceneRenderer::CollectAndUploadLights(Scene& scene)
{
    entt::registry& registry = scene.GetRegistry();
    GPULightUBO header{};
    GPULightData dirLights[1]{};
    GPULightData pointLights[16]{};

    // 收集方向光
    auto dirView = registry.view<TransformComponent, DirectionalLightComponent>();
    dirView.each([&](auto entity, TransformComponent& tc, DirectionalLightComponent& dl) {
        if (header.NumDirectionalLights >= MAX_DIRECTIONAL_LIGHTS) return;
        auto& light = dirLights[header.NumDirectionalLights];
        // 💡 方向需乘以实体旋转矩阵
        glm::vec3 dir = glm::normalize(glm::mat3(tc.GetTransform()) * dl.Direction);
        light.Direction = glm::vec4(dir, 0.0f);
        light.Color     = glm::vec4(dl.Color, dl.Intensity);
        light.Position  = glm::vec4(0.0f);
        light.CastShadow = dl.CastShadow ? 1 : 0;
        header.NumDirectionalLights++;
    });

    // 收集点光源
    auto ptView = registry.view<TransformComponent, PointLightComponent>();
    ptView.each([&](auto entity, TransformComponent& tc, PointLightComponent& pl) {
        if (header.NumPointLights >= MAX_POINT_LIGHTS) return;
        auto& light = pointLights[header.NumPointLights];
        light.Direction  = glm::vec4(0, 0, 0, 1.0f);
        light.Color      = glm::vec4(pl.Color, pl.Intensity);
        light.Position   = glm::vec4(tc.Translation, pl.Radius);
        light.CastShadow = pl.CastShadow ? 1 : 0;
        header.NumPointLights++;
    });

    // 上传到 GPU
    uint32_t offset = 0;
    s_LightUBO->SetData(&header, sizeof(GPULightUBO), offset);
    offset += sizeof(GPULightUBO);  // 16

    s_LightUBO->SetData(dirLights, sizeof(GPULightData) * MAX_DIRECTIONAL_LIGHTS, offset);
    offset += MAX_DIRECTIONAL_LIGHTS * sizeof(GPULightData);  // +64 = 80

    s_LightUBO->SetData(pointLights, sizeof(GPULightData) * header.NumPointLights, offset);
}
```

---

## 8. PBR 着色器详解

着色器文件：`RedflagForge/assets/shaders/Renderer3D_Mesh.glsl`

### 8.1 顶点着色器

```glsl
void main()
{
    // 1. 模型空间 → 世界空间
    vec4 worldPos = u_Transform * vec4(a_Position, 1.0);
    v_WorldPos = worldPos.xyz;
    v_TexCoord = a_TexCoord;

    // 2. 法线矩阵 = transpose(inverse(Model))
    //    非均匀缩放时不能直接用模型矩阵变换法线
    mat3 normalMatrix = mat3(transpose(inverse(u_Transform)));

    // 3. 变换法线和切线到世界空间
    vec3 N = normalize(normalMatrix * a_Normal);
    vec3 T = normalize(normalMatrix * a_Tangent);

    // 4. Gram-Schmidt 正交化
    T = normalize(T - dot(T, N) * N);

    // 5. 构造 TBN 矩阵（切线空间 → 世界空间）
    vec3 B = cross(N, T);
    v_TBN = mat3(T, B, N);

    // 6. 裁剪空间位置
    gl_Position = u_ViewProjection * worldPos;
}
```

**为什么需要 TBN 矩阵？**
法线贴图存储的法线是在切线空间（Tangent Space）中的，要在世界空间计算光照，就需要 TBN 矩阵把切线空间法线变换到世界空间。

### 8.2 片元着色器 — 表面属性采样

```glsl
// ── 基础颜色 ──
vec4 albedo4 = u_BaseColor;
if (u_UseBaseColorMap == 1) {
    vec4 texColor = texture(u_BaseColorMap, v_TexCoord);
    texColor.rgb = pow(texColor.rgb, vec3(2.2));  // sRGB → 线性空间
    albedo4 *= texColor;
}
vec3 albedo = albedo4.rgb;

// ── 金属度 & 粗糙度 ──
float metallic  = u_Metallic;
float roughness = u_Roughness;
if (u_UseMetallicRoughnessMap == 1) {
    vec4 mrSample = texture(u_MetallicRoughnessMap, v_TexCoord);
    roughness *= mrSample.g;  // G 通道 = 粗糙度 (glTF 规范)
    metallic  *= mrSample.b;  // B 通道 = 金属度
}
roughness = clamp(roughness, 0.04, 1.0);  // 防止 GGX 除零

// ── 环境遮蔽 ──
float ao = u_AO;
if (u_UseAOMap == 1) ao *= texture(u_AOMap, v_TexCoord).r;

// ── 法线 ──
vec3 N;
if (u_UseNormalMap == 1) {
    vec3 tangentNormal = texture(u_NormalMap, v_TexCoord).rgb * 2.0 - 1.0;
    N = normalize(v_TBN * tangentNormal);  // 切线空间 → 世界空间
} else {
    N = normalize(v_Normal);
}
```

### 8.3 片元着色器 — PBR Cook-Torrance BRDF

**完整 BRDF 公式：**

$$f(\mathbf{l}, \mathbf{v}) = k_D \cdot \frac{\text{albedo}}{\pi} + \frac{D \cdot G \cdot F}{4 \cdot (N \cdot V) \cdot (N \cdot L)}$$

其中：

| 符号 | 含义 | 实现函数 |
|------|------|----------|
| $D$ | **法线分布函数 (NDF)** — 微面元朝向 $H$ 的概率 | `DistributionGGX()` |
| $G$ | **几何遮蔽函数** — 微面元互相遮挡的比例 | `GeometrySmith()` |
| $F$ | **菲涅尔函数** — 掠射角反射率增加 | `FresnelSchlick()` |
| $k_D$ | 漫反射系数 = $(1 - F) \times (1 - \text{metallic})$ | — |

#### D: GGX / Trowbridge-Reitz

$$D(H) = \frac{\alpha^2}{\pi \cdot \left((N \cdot H)^2 \cdot (\alpha^2 - 1) + 1\right)^2}$$

```glsl
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a  = roughness * roughness;     // α = roughness² (Disney 重映射)
    float a2 = a * a;
    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    return a2 / max(denom, 0.0001);
}
```

#### G: Smith 几何遮蔽

$$G(N,V,L) = G_1(N,V) \times G_1(N,L)$$

$$G_1(N,V) = \frac{N \cdot V}{(N \cdot V)(1-k) + k}, \quad k = \frac{(\text{roughness}+1)^2}{8}$$

```glsl
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    return GeometrySchlickGGX(max(dot(N, V), 0.0), roughness)
         * GeometrySchlickGGX(max(dot(N, L), 0.0), roughness);
}
```

#### F: Fresnel-Schlick

$$F(\theta) = F_0 + (1 - F_0)(1 - \cos\theta)^5$$

```glsl
vec3 FresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}
```

其中 $F_0$（基础反射率）：
- 非金属：固定 `0.04`（塑料/石头等大多数介电质）
- 金属：取 `albedo` 颜色（金银铜等有色反射）
- metallic 作为插值权重：`F0 = mix(vec3(0.04), albedo, metallic)`

### 8.4 光照计算

#### 方向光

```glsl
vec3 CalcDirectionalLight(LightData light, vec3 N, vec3 V, vec3 albedo,
                          float metallic, float roughness, vec3 F0) {
    vec3 L = normalize(-light.Direction.xyz);      // 从表面指向光源
    vec3 H = normalize(V + L);                     // 半程向量
    vec3 radiance = light.Color.xyz * light.Color.w; // 无距离衰减

    float NDF = DistributionGGX(N, H, roughness);
    float G   = GeometrySmith(N, V, L, roughness);
    vec3  F   = FresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3  specular = (NDF * G * F) / (4.0 * max(dot(N,V), 0.0) * max(dot(N,L), 0.0) + 0.0001);
    vec3  kD = (vec3(1.0) - F) * (1.0 - metallic);  // 能量守恒

    float NdotL = max(dot(N, L), 0.0);
    return (kD * albedo / PI + specular) * radiance * NdotL;
}
```

#### 点光源

与方向光相同的 BRDF，但多了距离衰减：

```glsl
// UE4 风格平滑衰减：在半径边缘平滑降为 0
float t = clamp(1.0 - (distance²) / (radius²), 0.0, 1.0);
float attenuation = t * t;
vec3 radiance = light.Color.xyz * light.Color.w * attenuation;
```

### 8.5 后处理（临时方案）

```glsl
// 环境光 — 简单常数项
vec3 ambient = vec3(0.03) * albedo * ao;
vec3 color = ambient + Lo;

// Reinhard 色调映射：HDR → LDR
color = color / (color + vec3(1.0));

// Gamma 校正：线性 → sRGB
color = pow(color, vec3(1.0 / 2.2));

o_Color    = vec4(color, albedo4.a);
o_EntityID = u_EntityID;
```

---

## 9. 材质系统

### 9.1 Material 类

遵循 **glTF 2.0 PBR Metallic-Roughness** 工作流：

```cpp
class Material {
public:
    // PBR 参数
    glm::vec4      BaseColor{ 1.0f };      // 基础颜色因子
    Ref<Texture2D> BaseColorMap;            // 漫反射贴图 (sRGB)
    Ref<Texture2D> NormalMap;               // 切线空间法线贴图
    Ref<Texture2D> MetallicRoughnessMap;    // G=粗糙度, B=金属度
    Ref<Texture2D> AOMap;                   // R=环境遮蔽

    float Metallic  = 0.0f;   // [0,1] 金属度
    float Roughness = 0.5f;   // [0,1] 粗糙度
    float AO        = 1.0f;   // [0,1] 环境遮蔽强度

    AlphaMode Alpha       = AlphaMode::Opaque;
    float     AlphaCutoff = 0.5f;
    bool      DoubleSided = false;

    // 排序键 = BaseColorMap 指针，用于 Renderer3D 命令队列排序
    uintptr_t GetSortKey() const;
};
```

### 9.2 纹理槽位约定

| Slot | 用途 | 颜色空间 |
|------|------|----------|
| 0 | (2D) 白色纹理 / 纹理数组 0 | — |
| 1-31 | (2D) 纹理数组 | sRGB |
| 3 | (3D) BaseColorMap | sRGB → shader 手动 pow(2.2) |
| 4 | (3D) NormalMap | Linear |
| 5 | (3D) MetallicRoughnessMap | Linear |
| 6 | (3D) AOMap | Linear |

---

## 10. 模型加载 (Assimp)

### 10.1 加载流程

```cpp
Ref<Model> Model::Load(const std::string& filepath)
{
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(filepath,
        aiProcess_Triangulate       |  // 所有面转为三角形
        aiProcess_GenSmoothNormals  |  // 生成平滑法线
        aiProcess_CalcTangentSpace     // 计算切线（法线贴图必需）
    );
    // ...
    model->ProcessNode(scene->mRootNode, scene, glm::mat4(1.0f));
}
```

### 10.2 递归节点处理

```
aiScene
 └─ mRootNode (aiNode)
     ├─ mMeshes[0] → ProcessMesh() → SubMesh { MeshData, Mat, LocalTransform }
     ├─ mMeshes[1] → ProcessMesh() → SubMesh
     └─ children[]
         ├─ childNode → ProcessNode(parentTransform * localTransform)
         └─ ...
```

每个节点的局部变换累乘到 `SubMesh.LocalTransform`，在绘制时与实体的世界变换合并：
```cpp
glm::mat4 finalTransform = entityTransform * subMesh.LocalTransform;
```

### 10.3 PBR 材质提取

```
1. BaseColorMap     ← aiTextureType_DIFFUSE
2. NormalMap        ← aiTextureType_NORMALS (或 HEIGHT 回退)
3. MetallicRoughness ← aiTextureType_UNKNOWN (glTF)
4. AOMap            ← aiTextureType_LIGHTMAP

5. BaseColor 标量   ← AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_BASE_COLOR_FACTOR
                      (回退到 AI_MATKEY_COLOR_DIFFUSE)
6. Metallic 标量    ← AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_METALLIC_FACTOR
7. Roughness 标量   ← AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_ROUGHNESS_FACTOR
```

**FBX 黑色修复**：FBX 材质经常将传统 diffuse color 设为 (0,0,0)，当检测到 BaseColor 亮度 < 0.01 且有 BaseColorMap 时，自动重置为白色。

### 10.4 纹理路径搜索

```
搜索顺序：
1. modelDir/relPath        (Assimp 返回的相对路径)
2. modelDir/filename       (仅文件名)
3. modelDir/textures/filename
```

---

## 11. Shader 编译管线 (SPIR-V)

### 11.1 两阶段编译

```
                        shaderc                   spirv_cross              shaderc
GLSL 源码 ──────> Vulkan SPIR-V ──────> OpenGL GLSL (修正) ──────> OpenGL SPIR-V
                        ↓                                                  ↓
                  [缓存到磁盘]                                       [缓存到磁盘]
                  *.vulkan.spv                                       *.opengl.spv
```

### 11.2 详细流程

```cpp
// 阶段 1: GLSL → Vulkan SPIR-V
void CompileOrGetVulkanBinaries(shaderSources)
{
    shaderc::Compiler compiler;
    options.SetTargetEnvironment(shaderc_target_env_opengl, shaderc_env_version_opengl_4_5);
    options.SetOptimizationLevel(shaderc_optimization_level_performance);
    options.SetAutoMapLocations(true);     // 自动分配 location
    options.SetAutoBindUniforms(true);     // 自动分配 binding

    // 检查缓存：比较源文件和 .vulkan.spv 的修改时间
    if (cacheValid) { /* 读取缓存 */ }
    else { compiler.CompileGlslToSpv(...) → 写入缓存 }

    // SPIR-V 反射：打印 UBO 信息
    Reflect(stage, data);
}

// 阶段 2: Vulkan SPIR-V → OpenGL SPIR-V
void CompileOrGetOpenGLBinaries()
{
    spirv_cross::CompilerGLSL glslCompiler(vulkanSpirv);

    // 💡 关键：重命名 varying 为 "v_loc_N"，解决跨阶段名称不匹配
    renameByLocation(resources.stage_outputs / stage_inputs);

    std::string glslSource = glslCompiler.compile();
    // 再编译回 SPIR-V (OpenGL flavour)
    compiler.CompileGlslToSpv(glslSource, ...) → 写入 *.opengl.spv
}

// 阶段 3: 创建 OpenGL 程序
void CreateProgram()
{
    for (auto& [stage, spirv] : m_OpenGLSPIRV) {
        GLuint shaderID = glCreateShader(stage);
        glShaderBinary(1, &shaderID, GL_SHADER_BINARY_FORMAT_SPIR_V, ...);
        glSpecializeShader(shaderID, "main", 0, nullptr, nullptr);
        glAttachShader(program, shaderID);
    }
    glLinkProgram(program);
}
```

### 11.3 缓存路径

```
assets/cache/shader/opengl/
├── Renderer3D_Mesh.glslGL_VERTEX_SHADER.vulkan.spv
├── Renderer3D_Mesh.glslGL_VERTEX_SHADER.opengl.spv
├── Renderer3D_Mesh.glslGL_FRAGMENT_SHADER.vulkan.spv
├── Renderer3D_Mesh.glslGL_FRAGMENT_SHADER.opengl.spv
├── Renderer2D_Quad.glslGL_VERTEX_SHADER.vulkan.spv
├── ...
```

修改 shader 源码后，如果时间戳比缓存新，会自动重新编译。手动删除 `assets/cache/shader/opengl/` 下的 `.spv` 文件可强制重编译。

---

## 12. 帧缓冲与 Entity ID 拾取

### 12.1 FBO 配置

```cpp
FramebufferSpecification fbSpec;
fbSpec.Attachments = {
    FramebufferTextureFormat::RGBA8,         // attachment 0: 场景颜色
    FramebufferTextureFormat::RED_INTEGER,   // attachment 1: Entity ID (int)
    FramebufferTextureFormat::Depth          // depth: DEPTH24STENCIL8
};
fbSpec.Width  = 1600;
fbSpec.Height = 900;
m_Framebuffer = Framebuffer::Create(fbSpec);
```

### 12.2 Entity ID 写入

所有着色器的片元阶段都输出 `o_EntityID`：
- **Quad/Circle**: `o_EntityID = v_EntityID`（从 CPU 端 DrawSprite 传入）
- **PBR Mesh**: `o_EntityID = u_EntityID`（从 Material UBO 读取）
- **Line**: `o_EntityID = -1`（线段不可拾取）

### 12.3 鼠标拾取

```cpp
// ForgeLayer::OnUpdate 末尾
auto [mx, my] = ImGui::GetMousePos();
mx -= m_ViewportBounds[0].x;
my -= m_ViewportBounds[0].y;
my = viewportSize.y - my;  // 翻转 Y (ImGui 左上=0, OpenGL 左下=0)

int pixelData = m_Framebuffer->ReadPixel(1, mouseX, mouseY);  // 读 attachment 1
m_HoveredEntity = pixelData >= 0
    ? Entity{(entt::entity)pixelData, m_ActiveScene.get()}
    : Entity{};
```

---

## 13. 编辑器覆盖层 (Gizmo)

编辑模式下，在 `SceneRenderer::RenderScene` 之后，`ForgeLayer` 额外绘制编辑器辅助图形：

```
Renderer2D::BeginScene(editorCamera)
│
├── 相机图标 — DrawCameraGizmo()
│   绘制机身矩形 + 镜头锥台线框
│
├── 方向光图标 — DrawDirectionalLightGizmo()
│   绘制圆环 + 5 条射线（表示光照方向）
│   💡 方向 = glm::mat3(tc.GetTransform()) * dl.Direction（应用实体旋转）
│
├── 点光源图标 — DrawPointLightGizmo()
│   绘制三个正交圆环 + 6 条十字射线
│
├── BoxCollider2D 线框 — 绿色矩形
│
└── CircleCollider2D 线框 — 绿色 32 段圆
│
Renderer2D::EndScene()
```

所有 Gizmo 使用 `Renderer2D::DrawLine()` 绘制，统一在 Line 批次中提交。

---

## 14. ECS 组件一览

| 组件 | 用途 | 关键字段 |
|------|------|----------|
| `IDComponent` | 全局唯一 ID | `UUID ID` |
| `TagComponent` | 实体名称 | `string Tag` |
| `TransformComponent` | 位置/旋转/缩放 | `Translation, Rotation, Scale` → `GetTransform()` |
| `SpriteRendererComponent` | 2D 精灵 | `Color, Texture, TilingFactor` |
| `CircleRendererComponent` | 2D 圆 | `Color, Thickness, Fade` |
| `CameraComponent` | 场景相机 | `SceneCamera, Primary, FixedAspectRatio` |
| `NativeScriptComponent` | C++ 脚本 | `Instance, InstantiateScript, DestroyScript` |
| `RigidBody2DComponent` | 2D 刚体 | `BodyType(Static/Dynamic/Kinematic), FixedRotation` |
| `BoxCollider2DComponent` | 2D 方形碰撞体 | `Offset, Size, Density, Friction, Restitution` |
| `CircleCollider2DComponent` | 2D 圆形碰撞体 | `Offset, Radius, Density, Friction, Restitution` |
| `MeshRendererComponent` | 3D 单网格 | `MeshData, Color, Texture, Mat` |
| `ModelRendererComponent` | 3D 多网格模型 | `FilePath, ModelAsset, Color, OverrideMaterial` |
| `DirectionalLightComponent` | 方向光 | `Direction, Color, Intensity, CastShadow` |
| `PointLightComponent` | 点光源 | `Color, Intensity, Radius, CastShadow` |

---

## 15. 附录：std140 对齐规则备忘

在 OpenGL 的 `std140` 布局中，UBO 成员的对齐规则经常导致 C++↔GLSL 布局不匹配：

| GLSL 类型 | 对齐 (alignment) | 大小 (size) |
|-----------|------------------|-------------|
| `int` / `float` | 4 bytes | 4 bytes |
| `vec2` | 8 bytes | 8 bytes |
| `vec3` | **16 bytes** | 12 bytes |
| `vec4` | 16 bytes | 16 bytes |
| `mat4` | 16 bytes (每列) | 64 bytes |
| `int[N]` / `float[N]` | **16 bytes (每个元素!)** | N × 16 bytes |
| `struct` | **对齐到其最大成员的对齐值，再向上取 vec4 (16) 的倍数** | — |

### ⚠️ 最大陷阱：标量/vec2 数组

```glsl
// GLSL std140:
int _pad[3];
// 不是 3 × 4 = 12 字节！
// 每个元素对齐到 vec4 → 3 × 16 = 48 字节！

// 正确做法：用独立变量
int _pad0;   // 4 bytes
int _pad1;   // 4 bytes
int _pad2;   // 4 bytes
// 总计 12 字节，与 C++ int[3] 匹配
```

### 验证方法

```
C++ sizeof(GPULightUBO) == 16  ←→  Shader header 应占 16 字节
C++ sizeof(GPULightData) == 64 ←→  Shader LightData 应占 64 字节
```

如果不匹配，灯光数据会从错误偏移读取，导致光照完全异常（模型全黑或颜色错乱）。

---

## 总结：一帧 PBR 3D 物体的完整数据流

```
FBX/OBJ 文件
    │ Assimp 加载
    ▼
Model (SubMesh[] + Material[])
    │ Renderer3D::DrawModel
    ▼
DrawCommand 队列 { Mesh, Transform, Material, EntityID }
    │ EndScene() → 按材质排序
    ▼
┌─────────────────┐
│ 遍历 DrawCommand │
│                  │
│ 1. Bind Shader   │  ← Renderer3D_Mesh.glsl (一帧只绑定一次)
│                  │
│ 2. Upload Camera │  ← UBO binding=0 (一帧一次)
│    { VP, CamPos }│
│                  │
│ 3. Upload Lights │  ← UBO binding=3 (一帧一次, 由 SceneRenderer)
│    { 1 dir, N pt}│
│                  │
│ for each cmd:    │
│ ├ Upload Material│  ← UBO binding=2 (材质变化时)
│ │ { BaseColor,   │     + 绑定 4 张 PBR 贴图到 slot 3-6
│ │   Metallic,    │
│ │   Roughness,   │
│ │   AO, flags }  │
│ │                │
│ ├ Upload Transform ← UBO binding=1 (每次 DrawCall)
│ │ { mat4 Model } │
│ │                │
│ └ DrawIndexed()  │  ← glDrawElements(GL_TRIANGLES)
│                  │
│   GPU Pipeline:  │
│   ┌─ Vertex ─┐   │
│   │ Model→World│  │  法线矩阵、TBN 矩阵构造
│   │ World→Clip │  │
│   └────┬──────┘   │
│        ▼          │
│   ┌─ Fragment ──┐ │
│   │ 采样 4 张贴图│ │  BaseColor(sRGB→线性), Normal, MR, AO
│   │ 构造 N,V,F0 │ │
│   │ for each光源:│ │  CalcDirectionalLight / CalcPointLight
│   │   D×G×F BRDF│ │  (GGX + Smith + Fresnel-Schlick)
│   │ + 环境光    │ │
│   │ Reinhard TM │ │  HDR → LDR
│   │ Gamma 2.2   │ │  线性 → sRGB
│   │ → o_Color   │ │
│   │ → o_EntityID│ │
│   └─────────────┘ │
└─────────────────┘
    │
    ▼
Framebuffer
 ├ attachment 0: RGBA8 → Viewport 显示
 ├ attachment 1: RED_INTEGER → 鼠标拾取
 └ depth: DEPTH24STENCIL8
```
