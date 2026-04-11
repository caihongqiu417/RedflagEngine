# 模型导入系统开发日志

---

## 一、整体架构思路

### 问题起点
阶段一已实现 `Mesh` + `Renderer3D`，可以画出程序化的立方体。阶段二目标是通过 Assimp 加载任意 3D 模型文件（.obj / .fbx / .gltf）。

### 核心设计决策

**是否把模型直接放组件里？**

直接把 `Ref<Model>` 放进组件会导致：同一个文件加载两个实体时内存中存在两份模型数据，资源生命周期混乱。

**方案：简单路径缓存 AssetManager**

```
ModelRendererComponent { FilePath; }
↓ 渲染时
AssetManager::GetOrLoad<Model>(FilePath)  →  命中缓存直接返回 / 否则调用 Model::Load()
```

- 同路径永远只有一份 `Ref<Model>`，由 `static unordered_map` 持有
- 组件只存路径字符串，场景序列化保存路径即可
- 不引入 UUID / 元数据文件等复杂机制，符合引擎现阶段体量

---

## 二、新增文件

### 1. `Redflag/src/Redflag/Core/AssetManager.h`

```cpp
template<typename T>
static Ref<T> GetOrLoad(const std::string& path)
{
    auto& cache = GetCache<T>();
    auto it = cache.find(path);
    if (it != cache.end())
        return it->second;

    Ref<T> asset = T::Load(path);
    if (asset)
        cache[path] = asset;
    return asset;
}
```

**设计要点：**
- 纯头文件，全模板。每个类型 T 拥有独立的 `static unordered_map`，通过 `GetCache<T>()` 获取，互不干扰
- 约定：被管理的资源类型必须提供 `static Ref<T> Load(const std::string& path)` 工厂方法
- 提供 `IsLoaded<T>`、`Unload<T>`、`Clear<T>` 辅助接口
- 用 `shared_ptr` 缓存：AssetManager 是资源最终持有者，生命周期清晰

---

### 2. `Redflag/src/Redflag/Renderer/Model.h`

```cpp
struct SubMesh {
    Ref<Mesh>      MeshData;
    Ref<Texture2D> DiffuseTexture;  // 可为 nullptr（无贴图时用颜色）
};

class Model {
public:
    const std::vector<SubMesh>& GetSubMeshes() const;
    const std::string& GetFilePath() const;
    static Ref<Model> Load(const std::string& filepath);  // AssetManager 约定接口
private:
    void ProcessNode(aiNode*, const aiScene*);
    SubMesh ProcessMesh(aiMesh*, const aiScene*);
    Ref<Texture2D> LoadMaterialTexture(aiMaterial*, const aiScene*, int type);

    std::vector<SubMesh> m_SubMeshes;
    std::string          m_FilePath;
    std::string          m_Directory;  // 模型目录，用于拼接贴图相对路径
};
```

**设计要点：**
- 头文件只前向声明 Assimp 类型（`struct aiNode` 等），不 include Assimp，避免污染引擎公共头
- Assimp 的使用完全封装在 .cpp 内部

---

### 3. `Redflag/src/Redflag/Renderer/Model.cpp`

**加载流程：**

```
Model::Load(filepath)
  └─ importer.ReadFile(filepath, flags)
       旗标：aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_CalcTangentSpace
       注意：不加 aiProcess_FlipUVs（见后文 UV 翻转问题说明）
  └─ ProcessNode(rootNode)
       └─ 递归遍历 aiNode 树
           └─ ProcessMesh(aiMesh)
                ├─ 提取顶点：Position / Normal / TexCoord[0]
                ├─ 提取面索引（已被 Triangulate 保证为三角形）
                ├─ Mesh::Create(vertices, indices)
                └─ LoadMaterialTexture(aiMaterial, DIFFUSE)
                     └─ 拼接 m_Directory + "/" + texPath  →  Texture2D::Create()
```

**UV 翻转问题（关键 Bug）：**

| 层 | 操作 | 原因 |
|---|---|---|
| `OpenGLTexture.cpp` | `stbi_set_flip_vertically_on_load(1)` | stb_image 默认 V=0 在顶部，OpenGL 要求 V=0 在底部 |
| ~~`aiProcess_FlipUVs`~~ | ~~UV V 轴翻转~~ | ~~以为 OBJ UV 需要翻转~~ |

实际上 OBJ 格式的 UV 已经是 OpenGL 约定（V=0 在底部），stb_image 翻转图像后两者完全匹配。若再加 `aiProcess_FlipUVs` 会导致 UV 被翻转两次，对于 Kenney 这类 colormap atlas 贴图，采样会落到颜色块以外的黑色边缘，造成模型全黑。

**结论：只用 stb_image 翻转，不加 `aiProcess_FlipUVs`。**

---

## 三、修改文件

### 4. `Redflag/src/Redflag/Renderer/Renderer3D.h / .cpp`

新增两个接口：

```cpp
// 运行时渲染用，传入已计算好的 ViewProjection 矩阵
static void BeginScene(const glm::mat4& viewProjection);

// 遍历 Model 的所有 SubMesh 依次绘制
static void DrawModel(const Ref<Model>& model, const glm::mat4& transform,
                      const glm::vec4& color = glm::vec4(1.0f));
```

`DrawModel` 实现：

```cpp
for (const auto& subMesh : model->GetSubMeshes()) {
    if (subMesh.DiffuseTexture)
        DrawMesh(subMesh.MeshData, transform, subMesh.DiffuseTexture);
    else
        DrawMesh(subMesh.MeshData, transform, color);  // 无贴图降级为纯色
}
```

---

### 5. `Redflag/src/Redflag/Scene/Components.h`

新增组件：

```cpp
struct ModelRendererComponent {
    std::string FilePath;    // 序列化持久化，唯一标识符
    Ref<Model>  ModelAsset;  // 运行时句柄，由 AssetManager 管理，不序列化
    glm::vec4   Color = glm::vec4(1.0f);  // 无贴图时的备用颜色
};
```

保留原有 `MeshRendererComponent`（用于程序化网格，如默认立方体），新增 `ModelRendererComponent` 用于文件加载模型，两者互不影响。

---

### 6. `Redflag/src/Redflag/Scene/Scene.cpp`

在 `OnUpdateEditor` 末尾追加：

```cpp
auto modelView = m_Registry.view<TransformComponent, ModelRendererComponent>();
modelView.each([&](auto entity, TransformComponent& transform, ModelRendererComponent& mr) {
    // 懒加载：首次渲染时通过 AssetManager 加载，后续帧直接使用缓存
    if (!mr.ModelAsset && !mr.FilePath.empty())
        mr.ModelAsset = AssetManager::GetOrLoad<Model>(mr.FilePath);

    if (mr.ModelAsset)
        Renderer3D::DrawModel(mr.ModelAsset, transform.GetTransform(), mr.Color);
});
```

在 `OnUpdateRuntime` 中同样追加（使用运行时主相机的 ViewProjection）：

```cpp
glm::mat4 viewProj3D = mainCamera->GetProjection() * glm::inverse(cameraTransform);
Renderer3D::BeginScene(viewProj3D);
// ... 同样的 ModelRendererComponent view
```

同时更新 `CopyComponent`（场景复制）和 `OnComponentAdded` 模板特化，保证新组件在 DuplicateEntity 时被正确复制。

---

### 7. `Redflag/src/Redflag/Scene/Panel/SceneHierachyPanel.cpp`

**Add Component 菜单：**

```cpp
if (!m_SelectionContext.HasComponent<ModelRendererComponent>())
    if (ImGui::MenuItem("Model Renderer"))
        m_SelectionContext.AddComponent<ModelRendererComponent>();
```

**DrawComponent UI：**

```cpp
DrawComponent<ModelRendererComponent>("Model Renderer", entity, [](auto& component) {
    ImGui::Text("File: %s", component.FilePath.empty() ? "(none)" : component.FilePath.c_str());

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_MODEL")) {
            component.FilePath   = (const char*)payload->Data;
            component.ModelAsset = AssetManager::GetOrLoad<Model>(component.FilePath);
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::ColorEdit4("Color", glm::value_ptr(component.Color));
    if (component.ModelAsset && ImGui::Button("Unload"))
        component.ModelAsset.reset();
});
```

---

### 8. `Redflag/src/Redflag/Scene/SceneSerializer.cpp`

**序列化（写入 YAML）：**

```yaml
ModelRendererComponent:
  FilePath: "D:/Cdev/Redflag/RedflagForge/assets/models/car.obj"
  Color: [1, 1, 1, 1]
```

**反序列化（读取 YAML）：**

```cpp
if (entity["ModelRendererComponent"]) {
    auto& mr  = entity.AddComponent<ModelRendererComponent>();
    mr.FilePath = node["FilePath"].as<std::string>();
    mr.Color    = node["Color"].as<glm::vec4>();
    // ModelAsset 不保存，首次渲染时由 AssetManager 懒加载
}
```

---

### 9. `Redflag/src/Redflag/Scene/Panel/ContentBrowserPanel.h / .cpp`

**拖拽系统重构（三次迭代）：**

| 版本 | 方案 | 问题 |
|---|---|---|
| v1 | `IsItemHovered + IsMouseDragging + SourceAllowNullID` | 鼠标离开 item 区域后 Hovered=false，payload 断 |
| v2 | `IsItemActive + IsMouseDragging + SourceAllowNullID` | drag 开始后 ActiveId 转交 drag 系统，原 item Active=false，payload 还是断 |
| v3 | `BeginDragDropSource()` 直接调用（无条件） | ImageButton 有合法 ID，ActiveId 机制正常，但跨窗口拖拽时 ActiveId 仍会丢失 |
| **v4（最终）** | **`SourceExtern` + 成员变量记录路径** | 完全不依赖 item 的 ActiveId，由自己的状态机驱动，跨窗口全程稳定 |

**最终方案原理：**

```cpp
// 头文件新增成员
std::string m_DragPath;
std::string m_DragType;

// OnImGuiRender 顶部（Begin 之后）每帧检查
if (!m_DragPath.empty()) {
    if (ImGui::IsMouseDown(Left)) {
        if (ImGui::BeginDragDropSource(SourceExtern)) {
            ImGui::SetDragDropPayload(m_DragType.c_str(), m_DragPath.c_str(), ...);
            ImGui::EndDragDropSource();
        }
    } else {
        m_DragPath.clear();  // 松开鼠标，清除状态
    }
}

// item 循环内只负责记录（鼠标按下时）
if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(Left)) {
    m_DragPath = fullPath;
    m_DragType = "CONTENT_BROWSER_MODEL";  // 或 TEXTURE / SCENE_FILE
}
```

`ImGuiDragDropFlags_SourceExtern` 的含义：payload 来自"外部"（非 ImGui item），每帧主动调用即可维持拖拽窗口，完全不依赖任何 item 的状态。

---

### 10. `Redflag/src/Redflag.h`

```cpp
#include "Redflag/Renderer/Model.h"
#include "Redflag/Core/AssetManager.h"
```

暴露给客户端（Sandbox / RedflagForge）使用。

---

## 四、使用方式

1. 在场景中创建空实体
2. Inspector → **Add Component → Model Renderer**
3. 从 Content Browser 将 `.obj` / `.fbx` / `.gltf` 文件**拖拽**到 Inspector 的 Model Renderer 面板上
4. 模型自动加载并渲染

**支持的模型格式：** `.obj` `.fbx` `.gltf` `.glb` `.dae` `.3ds`（Assimp 支持的格式均可）

**贴图自动加载：** 模型文件同目录下的漫反射贴图（`map_Kd`）会被自动加载，无需手动指定

---

## 五、已知限制与后续方向

| 限制 | 说明 | 后续方向 |
|---|---|---|
| 只加载漫反射贴图 | 法线贴图、高光贴图等未处理 | 阶段三 Material 系统 |
| 嵌入式纹理不支持 | FBX 内嵌纹理跳过，仅打 Warning | 实现 `Texture2D::CreateFromMemory` |
| 无光照 | shader 仅采样纹理或输出纯色 | 阶段三 Phong 光照 Shader |
| AssetManager 无上限 | 加载后永不释放（除非手动 Unload） | 引入 LRU 或引用计数清理策略 |
| 运行时相机不完整 | OnUpdateRuntime 使用主场景相机，不支持多相机选择 | 与 SceneCamera 系统打通 |

---

## 六、Bug 修复与渲染优化（2026-04-12）

### 6.1 修复：模型导入后子网格相对位置丢失

**问题**：`aiScene` 节点树中每个 `aiNode` 携带一个相对于父节点的局部变换矩阵 `mTransformation`，原实现在 `ProcessNode` 中完全忽略了这个矩阵，导致多部件模型（如角色身体各部位）全部叠加在原点。

**修改文件：** `Redflag/Renderer/Model.h`、`Model.cpp`

**方案（保留层级，存入 SubMesh）：**

```cpp
// SubMesh 新增字段
struct SubMesh {
    Ref<Mesh>     MeshData;
    Ref<Material> Mat;
    glm::mat4     LocalTransform{ 1.0f };  // 从根节点到本节点的累积变换
};

// ProcessNode 向下传递累积变换
void Model::ProcessNode(aiNode* node, const aiScene* scene, const glm::mat4& parentTransform)
{
    // aiMatrix4x4 是行主序，glm 是列主序，转置读取
    const aiMatrix4x4& am = node->mTransformation;
    glm::mat4 localMat(
        am.a1, am.b1, am.c1, am.d1,
        am.a2, am.b2, am.c2, am.d2,
        am.a3, am.b3, am.c3, am.d3,
        am.a4, am.b4, am.c4, am.d4
    );
    glm::mat4 nodeTransform = parentTransform * localMat;
    // ... 递归时传递 nodeTransform
}
```

`Renderer3D::DrawModel` 绘制每个 SubMesh 时：

```cpp
glm::mat4 finalTransform = transform * subMesh.LocalTransform;
```

---

### 6.2 修复：2D 场景导入后全黑

**根本原因一（UBO binding 覆盖）：**`Renderer2D` 和 `Renderer3D` 都在 binding=0 创建了 Camera UBO，`Renderer3D::Init()` 后执行，把 binding=0 的指向覆盖为 3D buffer，导致 2D shader 读到错误的相机矩阵。

**修改文件：** `Platform/OpenGL/OpenGLUniformBuffer.h / .cpp`

```cpp
// 新增成员记录 binding
uint32_t m_Binding = 0;

// SetData 时先重新绑回自己的插槽
void OpenGLUniformBuffer::SetData(const void* data, uint32_t size, uint32_t offset)
{
    glBindBufferBase(GL_UNIFORM_BUFFER, m_Binding, m_RendererID); // 先插回插槽
    glNamedBufferSubData(m_RendererID, offset, size, data);
}
```

**根本原因二（Flush 未绑 Shader 和 VAO）：**`Renderer2D::BeginScene(EditorCamera)` 路径从未调用 `TextureShader->Bind()`，`Flush()` 也没有绑定 `QuadVertexArray`，导致 OpenGL 沿用上一个（3D）shader 和 VAO 进行绘制。

**修改文件：** `Redflag/Renderer/Renderer2D.cpp`

```cpp
void Renderer2D::Flush()
{
    s_Data.TextureShader->Bind();        // 确保 2D shader 生效
    s_Data.QuadVertexArray->Bind();      // 确保 2D VAO 生效
    for (uint32_t i = 0; i < s_Data.TextureSlotIndex; i++)
        s_Data.TextureSlots[i]->Bind(i);
    RenderCommand::DrawIndexed(s_Data.QuadVertexArray, s_Data.QuadIndexCount);
    s_Data.Stas.DrawCalls++;
}
```

同时恢复 `Init()` 中被注释掉的 `SetIntArray("u_Textures", samplers, ...)` 调用，确保 sampler 数组正确映射。

---

### 6.3 优化：3D 渲染引入 Material 系统与 DrawCommand 排序队列

#### 背景与问题

原有实现中，`DrawModel` 遍历每个 SubMesh 后**立即调用 DrawCall**：

```cpp
// 原实现（伪代码）
void Renderer3D::DrawModel(model, transform, color)
{
    for (each subMesh in model)
    {
        BindTexture(subMesh.texture);              // GPU 状态切换
        UploadUBO(material_ubo, color, useTexture); // GPU 状态切换
        UploadUBO(transform_ubo, finalTransform);   // GPU 状态切换
        DrawCall();                                 // 执行绘制
    }
}
```

假设一个模型有 10 个 SubMesh，其中 8 个共用同一张贴图，就会发生 **10 次纹理绑定**，其中 8 次是完全无效的重复操作。GPU 状态切换（尤其是纹理绑定）是较昂贵的操作，在 SubMesh 数量多时会产生明显开销。

---

#### 解决思路：先收集，后排序，再执行

将"提交绘制请求"和"实际执行绘制"分离为两个阶段：

```
阶段一（BeginScene → DrawModel/DrawMesh 调用期间）
  每次 DrawMesh/DrawModel 只是把绘制信息打包成一条 DrawCommand 压入队列，不做任何 GPU 操作。

阶段二（EndScene）
  1. 对队列按材质排序（相同纹理的命令聚集在一起）
  2. 顺序执行：只在材质发生变化时才切换纹理和上传 Material UBO
  3. 每条命令仍需单独上传 Transform UBO 和执行 DrawCall（无法省略）
```

---

#### 新增：Material 类与 MaterialLibrary

**新增文件：** `Redflag/Renderer/Material.h / .cpp`

`Material` 封装了一个渲染单元需要的全部材质信息（纹理 + 颜色），同时提供一个 `GetSortKey()` 供排序使用：

```cpp
class Material
{
public:
    // 创建纯色材质
    static Ref<Material> Create(const glm::vec4& color);
    // 创建纹理材质（tint 为叠加颜色，默认白色即原色）
    static Ref<Material> Create(const Ref<Texture2D>& texture, const glm::vec4& tint = glm::vec4(1.0f));

    bool                  HasTexture() const;
    const Ref<Texture2D>& GetTexture() const;
    const glm::vec4&      GetColor()   const;

    // 排序键：以纹理的 GPU RendererID（GLuint）为键（nullptr = 纯色 = 0）
    // 用 RendererID 而非 CPU 指针的原因：
    //   glBindTexture 认的是 RendererID，两个不同的 Texture2D 对象可能包装同一个
    //   GPU 纹理（别名/视图），此时指针不同但 RendererID 相同，
    //   用指针排序会错误多出一次 glBindTexture，用 RendererID 才能正确跳过。
    uintptr_t GetSortKey() const { return m_Texture ? m_Texture->GetRendererID() : 0; }
};
```

`MaterialLibrary` 提供按名字注册和复用材质的能力，避免重复创建相同内容的材质对象：

```cpp
class MaterialLibrary
{
public:
    // 若已存在同名材质则直接返回，否则创建并注册
    static Ref<Material> GetOrCreate(const std::string& name, const Ref<Texture2D>& texture, ...);
    static Ref<Material> GetOrCreate(const std::string& name, const glm::vec4& color);
    static Ref<Material> Get(const std::string& name);  // 必须已存在，否则 assert
};
```

---

#### 修改：SubMesh 改用 Material

**修改文件：** `Redflag/Renderer/Model.h`

```cpp
// 修改前
struct SubMesh {
    Ref<Mesh>      MeshData;
    Ref<Texture2D> DiffuseTexture;  // 直接存纹理
    glm::mat4      LocalTransform;
};

// 修改后
struct SubMesh {
    Ref<Mesh>     MeshData;
    Ref<Material> Mat;              // 改为存材质，信息更完整，可扩展
    glm::mat4     LocalTransform;
};
```

`Model::ProcessMesh` 在加载时根据 Assimp 材质信息创建对应的 `Material` 对象：

```cpp
// 有漫反射贴图 → 纹理材质
if (diffuse)
    subMesh.Mat = Material::Create(diffuse);
// 无贴图 → Mat 留 nullptr，渲染时用调用方传入的颜色
```

---

#### 修改：Renderer3D 改为队列模式

**修改文件：** `Redflag/Renderer/Renderer3D.h / .cpp`

内部新增 `DrawCommand` 结构和命令队列：

```cpp
struct DrawCommand
{
    Ref<Mesh>     MeshData;
    glm::mat4     Transform;
    Ref<Material> Mat;       // nullptr 时使用默认白色纯色材质
};

// Renderer3DData 内部
std::vector<DrawCommand> DrawCommands;  // 每帧的提交队列，预分配 1024 容量
```

`DrawMesh` / `DrawModel` 只负责压队列：

```cpp
void Renderer3D::DrawModel(model, transform, color)
{
    auto fallback = Material::Create(color);  // 供无材质的 SubMesh 使用
    for (each subMesh in model)
    {
        glm::mat4 finalTransform = transform * subMesh.LocalTransform;
        Ref<Material> mat = subMesh.Mat ? subMesh.Mat : fallback;
        DrawCommands.push_back({ subMesh.MeshData, finalTransform, mat });
        // ← 不做任何 GPU 操作
    }
}
```

`EndScene` 负责排序并执行：

```cpp
void Renderer3D::EndScene()
{
    // 按纹理指针排序，相同材质的命令聚集到一起
    std::sort(DrawCommands, [](a, b){ return a.Mat->GetSortKey() < b.Mat->GetSortKey(); });

    s_Data.MeshShader->Bind();

    uintptr_t lastKey = ~0ull;  // 记录上一条命令的材质键
    for (each cmd in DrawCommands)
    {
        uintptr_t key = cmd.Mat->GetSortKey();

        if (key != lastKey)  // 材质发生变化才切换 GPU 状态
        {
            BindTexture(cmd.Mat);            // 纹理绑定（昂贵）
            UploadUBO(material_ubo, cmd.Mat); // Material UBO 上传
            lastKey = key;
        }

        // 每条命令必须单独更新（每个 SubMesh 变换不同）
        UploadUBO(transform_ubo, cmd.Transform);
        cmd.MeshData->Bind();
        DrawCall();
    }
}
```

---

#### 性能收益

以一个有 10 个 SubMesh、使用 2 种纹理的模型为例（8 个 SubMesh 共用纹理 A，2 个使用纹理 B）：

| GPU 操作 | 优化前 | 优化后 | 说明 |
|---|---|---|---|
| Shader 绑定 | 10 次 | 1 次 | EndScene 统一绑定一次 |
| 纹理绑定 | 10 次 | **2 次** | 排序后只在材质变化时切换 |
| Material UBO 上传 | 10 次 | **2 次** | 同上 |
| Transform UBO 上传 | 10 次 | 10 次 | 每个 SubMesh 变换不同，无法省略 |
| DrawCall | 10 次 | 10 次 | 不同 VAO 无法合并（未来可用 Instancing 优化）|

SubMesh 越多、材质种类越少，收益越明显。

---

#### 后续可扩展方向

- **Instancing**：同一 Mesh + 同一材质的多个实例，可合并为一次 `glDrawElementsInstanced`，真正减少 DrawCall
- **材质属性扩展**：`Material` 未来可增加法线贴图、粗糙度、金属度等 PBR 属性，渲染无需改动接口
- **多 Pass 排序**：在排序键中加入 Pass 层（如先渲染不透明，再渲染透明），实现正确的渲染顺序
