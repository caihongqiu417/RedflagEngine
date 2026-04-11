# Redflag 3D 渲染功能开发待办

## 阶段一：基础 3D 渲染 [目标：画出一个立方体]

- [x] **创建 Mesh 类** (`Redflag/src/Redflag/Renderer/Mesh.h / Mesh.cpp`)
  - 顶点结构体：位置(glm::vec3)、法线(glm::vec3)、纹理坐标(glm::vec2)
  - 内部持有 VertexArray、VertexBuffer、IndexBuffer
  - 先用硬编码的立方体顶点数据测试，不做文件加载

- [x] **编写 3D MVP Shader** (`RedflagForge/assets/shaders/Renderer3D_Mesh.glsl`)
  - Vertex Shader 接收：u_ViewProjection(mat4)、u_Transform(mat4)
  - Fragment Shader：采样纹理或输出纯色
  - 参考已有 2D shader 的格式

- [x] **创建 Renderer3D 类** (`Redflag/src/Redflag/Renderer/Renderer3D.h / Renderer3D.cpp`)
  - `Init()` / `Shutdown()`
  - `BeginScene(EditorCamera&)` — 复用已有的 EditorCamera
  - `DrawMesh(const Ref<Mesh>&, const glm::mat4& transform)`
  - `EndScene()`

- [x] **在 Renderer.h 中注册 Renderer3D**
  - 在 `Renderer::Init()` 和 `Renderer::Shutdown()` 中调用 Renderer3D 对应方法

- [x] **在 RedflagForge 的 ForgeLayer 中测试**
  - 创建一个立方体 Mesh，在 OnUpdate 中调用 Renderer3D::DrawMesh
  - 用 EditorCamera（已有）观察效果
  - **里程碑：屏幕上出现一个静态立方体** ✓

---

## 阶段二：模型加载 [目标：加载外部 .obj / .fbx 文件]

- [ ] **集成 Assimp 库**
  - 在 premake5.lua 中添加 Assimp 依赖
  - 将 Assimp 头文件和 lib 放入 `Redflag/vendor/assimp/`

- [ ] **扩展 Mesh 类支持从 Assimp 数据构建**
  - `Mesh::Create(const aiMesh* mesh)` 静态工厂

- [ ] **创建 Model 类** (`Redflag/src/Redflag/Renderer/Model.h / Model.cpp`)
  - 持有多个 Mesh（一个模型可以有多个子网格）
  - `Model::Load(const std::string& filepath)` — 使用 Assimp 解析文件
  - **里程碑：加载 .obj 文件并显示** ✓

---

## 阶段三：光照系统 [目标：有明暗感的真实画面]

- [ ] **Phong 光照 Shader**
  - 支持：环境光(Ambient)、漫反射(Diffuse)、镜面高光(Specular)
  - Uniform 数据：光源位置、光源颜色、相机位置

- [ ] **创建 Light 组件**
  - 添加到 ECS Scene 中（参考已有的 `SpriteRendererComponent` 结构）
  - 支持：点光源(Point Light)、平行光(Directional Light)

- [ ] **Material 系统**
  - 封装 Shader + Texture + 光照参数
  - `Material::Create(shader, albedoTexture, ...)`
  - **里程碑：模型有正确光照和阴影变化** ✓

---

## 注意事项

- 所有新类的 `Create()` 工厂方法参考 `Texture2D::Create()` 的写法（返回 `Ref<T>`）
- Buffer 布局参考 `Renderer2D` 中 `QuadVertex` 的定义方式
- Shader Uniform 传递参考 `Renderer2D::BeginScene` 中 UniformBuffer 的用法
