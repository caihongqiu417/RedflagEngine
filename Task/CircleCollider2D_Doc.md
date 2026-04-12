# CircleCollider2DComponent 集成与可视化文档

## 目录

1. [功能概述](#1-功能概述)
2. [涉及文件总览](#2-涉及文件总览)
3. [Step 1 — 定义组件结构体](#step-1--定义组件结构体)
4. [Step 2 — 接入场景系统（Scene.cpp）](#step-2--接入场景系统scenecpp)
5. [Step 3 — 场景序列化与反序列化](#step-3--场景序列化与反序列化)
6. [Step 4 — 编辑器面板（SceneHierachyPanel）](#step-4--编辑器面板scenehierachypanel)
7. [Step 5 — 编辑器碰撞体可视化（ForgeLayer）](#step-5--编辑器碰撞体可视化forgelayer)
8. [运行流程图](#8-运行流程图)
9. [常见问题 & 注意事项](#9-常见问题--注意事项)

---

## 1. 功能概述

**CircleCollider2DComponent** 是一个 2D 圆形物理碰撞体组件，挂载到 Entity 上后可以：

- 与 Box2D 物理引擎集成，使实体能够参与圆形碰撞检测
- 在编辑模式下以绿色线框实时预览碰撞范围，选中时高亮为黄色
- 通过编辑器 Inspector 面板直接调整碰撞参数
- 跟随场景完整地序列化（保存）与反序列化（加载）

它的设计方式和 **BoxCollider2DComponent** 完全对称，理解一个就能理解另一个。

---

## 2. 涉及文件总览

| 文件 | 作用 |
|------|------|
| `Redflag/src/Redflag/Scene/Components.h` | 定义组件结构体和字段 |
| `Redflag/src/Redflag/Scene/Scene.cpp` | 运行时 Box2D 物理创建、场景复制、DuplicateEntity |
| `Redflag/src/Redflag/Scene/SceneSerializer.cpp` | YAML 序列化（保存）和反序列化（加载） |
| `Redflag/src/Redflag/Scene/Panel/SceneHierachyPanel.cpp` | Inspector 面板 + Add Component 菜单 |
| `RedflagForge/src/ForgeLayer.h` | 编辑器 toggle 标志 |
| `RedflagForge/src/ForgeLayer.cpp` | 编辑模式下碰撞体线框可视化 |

---

## Step 1 — 定义组件结构体

**文件：** `Redflag/src/Redflag/Scene/Components.h`

```cpp
// 2D 圆形碰撞体组件
// 对应 Box2D 的 b2CircleShape，用于圆形刚体的碰撞检测
struct CircleCollider2DComponent
{
    // 碰撞圆心相对于实体 Transform 原点的偏移（局部空间）
    // 例如设为 (0, 0.5) 可把碰撞圆上移半个单位，常用于人物头部/脚部碰撞
    glm::vec2 Offset = { 0.f, 0.f };

    // 碰撞圆的半径（局部空间，默认 0.5）
    // 运行时会额外乘以实体 Scale 才传给 Box2D
    float Radius = 0.5f;

    // 密度（kg/m²），影响刚体的质量；设为 0 时视为传感器（无质量，只触发重叠事件）
    float Density = 1.f;

    // 摩擦系数 [0, 1]，0 = 完全光滑，1 = 最大摩擦
    float Friction = 0.5f;

    // 恢复系数（弹性）[0, 1]，0 = 完全非弹性碰撞，1 = 完全弹性反弹
    float Restitution = 0.f;

    // 恢复生效的最低碰撞速度阈值（m/s）
    // 碰撞速度低于此值时弹性不生效，避免静止物体微小抖动
    float RestitutionThreshold = 0.5f;

    // 运行时由 Box2D 填充的 b2Fixture* 指针，不序列化
    void* RuntimeFixture = nullptr;

    CircleCollider2DComponent() = default;
    CircleCollider2DComponent(const CircleCollider2DComponent&) = default;
};
```

**为什么需要 RuntimeFixture？**

Box2D 在 `OnRuntimeStart` 时会创建物理对象，并把 `b2Fixture*` 指针存在这里。  
运行时若需要动态修改物理属性（比如改摩擦），可以通过这个指针直接操作，而不必重建整个物理体。  
它**不参与序列化**，因为每次 Play 都会重新创建。

---

## Step 2 — 接入场景系统（Scene.cpp）

### 2.1 引入 Box2D 圆形头文件

**文件：** `Scene.cpp` 顶部

```cpp
#include "box2d/b2_world.h"
#include "box2d/b2_fixture.h"
#include "box2d/b2_polygon_shape.h"
#include "box2d/b2_circle_shape.h"   // ← 新增：圆形碰撞所需
```

### 2.2 OnRuntimeStart —— 创建 Box2D 物理 Fixture

按下 Play 时，`OnRuntimeStart` 遍历所有有 `RigidBody2DComponent` 的 Entity，为其创建 Box2D 刚体。  
在盒形碰撞处理之后，紧接着处理圆形碰撞：

```cpp
void Scene::OnRuntimeStart()
{
    m_PhysicsWorld = new b2World({ 0.f, -9.8f });  // 重力向下
    auto view = m_Registry.view<RigidBody2DComponent>();

    for (auto e : view)
    {
        Entity entity = { e, this };
        auto& transform = entity.GetComponent<TransformComponent>();
        auto& rb2d      = entity.GetComponent<RigidBody2DComponent>();

        // 1. 用实体当前位置和旋转创建 Box2D 刚体
        b2BodyDef bodyDef;
        bodyDef.type     = RigidBodyTypeToBox2DBody(rb2d.Type);
        bodyDef.position.Set(transform.Translation.x, transform.Translation.y);
        bodyDef.angle    = transform.Rotation.z;

        b2Body* body = m_PhysicsWorld->CreateBody(&bodyDef);
        body->SetFixedRotation(rb2d.FixedRotation);

        // 2. BoxCollider2D：多边形（矩形）形状
        if (entity.HasComponent<BoxCollider2DComponent>())
        {
            auto& bc2d = entity.GetComponent<BoxCollider2DComponent>();
            b2PolygonShape boxShape;
            // SetAsBox 的参数是半宽/半高，所以直接用 Size（默认 0.5 即表示总宽 1 个单位）
            boxShape.SetAsBox(transform.Scale.x * bc2d.Size.x,
                              transform.Scale.y * bc2d.Size.y);

            b2FixtureDef fixtureDef;
            fixtureDef.shape                = &boxShape;
            fixtureDef.density              = bc2d.Density;
            fixtureDef.friction             = bc2d.Friction;
            fixtureDef.restitution          = bc2d.Restitution;
            fixtureDef.restitutionThreshold = bc2d.RestitutionThreshold;
            body->CreateFixture(&fixtureDef);
        }

        // 3. CircleCollider2D：圆形形状
        if (entity.HasComponent<CircleCollider2DComponent>())
        {
            auto& cc2d = entity.GetComponent<CircleCollider2DComponent>();

            b2CircleShape circleShape;
            // 圆心偏移（局部空间，Box2D 会在刚体坐标系里应用）
            circleShape.m_p.Set(cc2d.Offset.x, cc2d.Offset.y);
            // 半径 = 组件 Radius × 实体缩放（取 x/y 较大值，保持圆形不被椭圆化）
            circleShape.m_radius = glm::max(transform.Scale.x, transform.Scale.y) * cc2d.Radius;

            b2FixtureDef fixtureDef;
            fixtureDef.shape                = &circleShape;
            fixtureDef.density              = cc2d.Density;
            fixtureDef.friction             = cc2d.Friction;
            fixtureDef.restitution          = cc2d.Restitution;
            fixtureDef.restitutionThreshold = cc2d.RestitutionThreshold;
            body->CreateFixture(&fixtureDef);
        }

        rb2d.RuntimeBody = body;  // 保存指针供每帧同步 Transform 用
    }
}
```

> **关键点：为什么半径取 max(Scale.x, Scale.y)？**  
> Box2D 只支持真正的圆（不支持椭圆）。如果实体的 x/y 缩放不同，不能同时满足两个方向，  
> 取较大值可以保证碰撞范围至少覆盖实体的最大维度，避免漏检。  
> 如需精确椭圆碰撞需要用多个圆形或多边形近似。

### 2.3 Scene::Copy —— Play 时复制场景

按下 Play 时，引擎会把编辑器场景复制成运行时场景。  
**如果这里漏掉一个组件，Play 后该组件就会消失。**

```cpp
// Scene::Copy 内部的组件复制列表
CopyComponent<TransformComponent>       (dstRegistry, srcRegistry, enttMap);
CopyComponent<SpriteRendererComponent>  (dstRegistry, srcRegistry, enttMap);
CopyComponent<CircleRendererComponent>  (dstRegistry, srcRegistry, enttMap);  // 圆形渲染
CopyComponent<CameraComponent>          (dstRegistry, srcRegistry, enttMap);
CopyComponent<NativeScriptComponent>    (dstRegistry, srcRegistry, enttMap);
CopyComponent<RigidBody2DComponent>     (dstRegistry, srcRegistry, enttMap);
CopyComponent<BoxCollider2DComponent>   (dstRegistry, srcRegistry, enttMap);
CopyComponent<CircleCollider2DComponent>(dstRegistry, srcRegistry, enttMap);  // ← 圆形碰撞
CopyComponent<MeshRendererComponent>    (dstRegistry, srcRegistry, enttMap);
CopyComponent<ModelRendererComponent>   (dstRegistry, srcRegistry, enttMap);
```

### 2.4 DuplicateEntity —— Ctrl+D 复制实体

```cpp
CopyComponentIfExist<RigidBody2DComponent>     (newEntity, entity);
CopyComponentIfExist<BoxCollider2DComponent>   (newEntity, entity);
CopyComponentIfExist<CircleCollider2DComponent>(newEntity, entity);  // ← 圆形碰撞
CopyComponentIfExist<NativeScriptComponent>    (newEntity, entity);
```

### 2.5 OnComponentAdded 模板特化

每次通过 `entity.AddComponent<T>()` 添加组件时会调用此函数（可在此做初始化回调，目前留空）：

```cpp
template<>
void Scene::OnComponentAdded<CircleCollider2DComponent>(Entity entity, CircleCollider2DComponent& component)
{
    // 目前无需额外初始化
}
```

---

## Step 3 — 场景序列化与反序列化

**文件：** `SceneSerializer.cpp`

YAML 是一种人类可读的数据格式，引擎用它来保存和加载 `.redflag` 场景文件。

### 3.1 序列化（保存）

```cpp
if (entity.HasComponent<CircleCollider2DComponent>())
{
    out << YAML::Key << "CircleCollider2DComponent";
    out << YAML::BeginMap;

    auto& cc2d = entity.GetComponent<CircleCollider2DComponent>();
    out << YAML::Key << "Offset"               << YAML::Value << cc2d.Offset;
    out << YAML::Key << "Radius"               << YAML::Value << cc2d.Radius;
    out << YAML::Key << "Density"              << YAML::Value << cc2d.Density;
    out << YAML::Key << "Friction"             << YAML::Value << cc2d.Friction;
    out << YAML::Key << "Restitution"          << YAML::Value << cc2d.Restitution;
    out << YAML::Key << "RestitutionThreshold" << YAML::Value << cc2d.RestitutionThreshold;
    // RuntimeFixture 不保存，每次 Play 重新创建

    out << YAML::EndMap;
}
```

保存到文件中的样子（`.redflag` 场景文件片段）：

```yaml
CircleCollider2DComponent:
  Offset: [0, 0]
  Radius: 0.5
  Density: 1
  Friction: 0.5
  Restitution: 0
  RestitutionThreshold: 0.5
```

### 3.2 反序列化（加载）

```cpp
if (entity["CircleCollider2DComponent"])
{
    auto ccNode = entity["CircleCollider2DComponent"];
    auto& cc2d = deserializedEntity.AddComponent<CircleCollider2DComponent>();

    cc2d.Offset               = ccNode["Offset"].as<glm::vec2>();
    cc2d.Radius               = ccNode["Radius"].as<float>();
    cc2d.Density              = ccNode["Density"].as<float>();
    cc2d.Friction             = ccNode["Friction"].as<float>();
    cc2d.Restitution          = ccNode["Restitution"].as<float>();
    cc2d.RestitutionThreshold = ccNode["RestitutionThreshold"].as<float>();
    // RuntimeFixture 由 OnRuntimeStart 在运行时自动填充
}
```

---

## Step 4 — 编辑器面板（SceneHierachyPanel）

**文件：** `SceneHierachyPanel.cpp`

### 4.1 Add Component 菜单

右键实体 → Add Component，出现 "Circle Collider 2D" 选项：

```cpp
if (!m_SelectionContext.HasComponent<CircleCollider2DComponent>())
{
    if (ImGui::MenuItem("Circle Collider 2D"))
    {
        m_SelectionContext.AddComponent<CircleCollider2DComponent>();
        ImGui::CloseCurrentPopup();
    }
}
```

`HasComponent` 检查确保同一个实体不会重复添加相同组件。

### 4.2 Inspector 属性面板

选中实体后，右侧 Inspector 会显示所有组件的可编辑属性：

```cpp
DrawComponent<CircleCollider2DComponent>("Circle Collider 2D", entity, [](auto& component)
{
    // 圆心偏移：拖动两个浮点数（X, Y）
    ImGui::DragFloat2("Offset", glm::value_ptr(component.Offset));

    // 半径：下限为 0.001 防止出现零半径导致 Box2D 断言错误
    ImGui::DragFloat("Radius", &component.Radius, 0.01f, 0.001f, FLT_MAX);

    // 以下物理参数范围 0~1
    ImGui::DragFloat("Density",              &component.Density,              0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("Friction",             &component.Friction,             0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("Restitution",          &component.Restitution,          0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("Restitution Threshold",&component.RestitutionThreshold, 0.01f, 0.0f);
});
```

---

## Step 5 — 编辑器碰撞体可视化（ForgeLayer）

这是最直观的部分：在编辑模式下用线框实时显示碰撞体范围，方便调整位置和大小。

### 5.1 添加开关标志

**文件：** `ForgeLayer.h`

```cpp
// 碰撞体可视化开关（编辑模式下显示绿色线框）
bool m_ShowPhysicsColliders = true;
```

### 5.2 View 菜单开关

**文件：** `ForgeLayer.cpp`，菜单栏部分：

```cpp
if (ImGui::BeginMenu("View"))
{
    // 勾选/取消勾选即可开关所有碰撞体线框
    ImGui::MenuItem("Physics Colliders", nullptr, &m_ShowPhysicsColliders);
    ImGui::EndMenu();
}
```

### 5.3 BoxCollider2D 矩形线框

```cpp
if (m_ShowPhysicsColliders)
{
    Entity sel = m_SceneHierachyPanel.GetSelectedEntity();

    // --- BoxCollider2D：4 条线段画矩形 ---
    auto bcView = m_ActiveScene->GetRegistry()
                    .view<TransformComponent, BoxCollider2DComponent>();

    bcView.each([&](auto entity, TransformComponent& tc, BoxCollider2DComponent& bc)
    {
        Entity ent = { entity, m_ActiveScene.get() };
        // 选中的实体用黄色，其余用绿色
        glm::vec4 col = (sel && sel == ent)
            ? glm::vec4(1.0f, 0.7f, 0.0f, 1.0f)   // 黄色
            : glm::vec4(0.0f, 1.0f, 0.0f, 0.8f);  // 绿色

        glm::mat4 t = tc.GetTransform();
        // xf：把局部坐标点变换到世界坐标
        auto xf = [&](glm::vec3 v) { return glm::vec3(t * glm::vec4(v, 1.0f)); };

        float ox = bc.Offset.x, oy = bc.Offset.y;
        float sx = bc.Size.x,   sy = bc.Size.y;

        // 4 个角点（z 偏移 0.001 防止 Z-fighting，避免线框被遮挡）
        glm::vec3 p0 = xf({ ox - sx, oy - sy, 0.001f });  // 左下
        glm::vec3 p1 = xf({ ox + sx, oy - sy, 0.001f });  // 右下
        glm::vec3 p2 = xf({ ox + sx, oy + sy, 0.001f });  // 右上
        glm::vec3 p3 = xf({ ox - sx, oy + sy, 0.001f });  // 左上

        Renderer2D::DrawLine(p0, p1, col);  // 底边
        Renderer2D::DrawLine(p1, p2, col);  // 右边
        Renderer2D::DrawLine(p2, p3, col);  // 顶边
        Renderer2D::DrawLine(p3, p0, col);  // 左边
    });
```

### 5.4 CircleCollider2D 圆形线框

用 **32 段折线** 近似一个圆（视觉上已足够圆滑，段数越多越圆但消耗越高）：

```cpp
    // --- CircleCollider2D：32 段折线近似圆 ---
    auto ccView = m_ActiveScene->GetRegistry()
                    .view<TransformComponent, CircleCollider2DComponent>();

    ccView.each([&](auto entity, TransformComponent& tc, CircleCollider2DComponent& cc)
    {
        Entity ent = { entity, m_ActiveScene.get() };
        glm::vec4 col = (sel && sel == ent)
            ? glm::vec4(1.0f, 0.7f, 0.0f, 1.0f)
            : glm::vec4(0.0f, 1.0f, 0.0f, 0.8f);

        glm::mat4 t = tc.GetTransform();
        auto xf = [&](glm::vec3 v) { return glm::vec3(t * glm::vec4(v, 1.0f)); };

        constexpr int   segments = 32;
        constexpr float twoPi    = glm::two_pi<float>();  // = 2π ≈ 6.2832
        float ox = cc.Offset.x, oy = cc.Offset.y;
        float r  = cc.Radius;

        for (int i = 0; i < segments; ++i)
        {
            // 将 360° 均匀分成 32 份，每份对应一段弧
            float a0 = twoPi * (float)i       / (float)segments;  // 起始角
            float a1 = twoPi * (float)(i + 1) / (float)segments;  // 结束角

            // 局部坐标（圆心 + 极坐标转直角坐标）
            glm::vec3 v0 = xf({ ox + r * glm::cos(a0), oy + r * glm::sin(a0), 0.001f });
            glm::vec3 v1 = xf({ ox + r * glm::cos(a1), oy + r * glm::sin(a1), 0.001f });

            Renderer2D::DrawLine(v0, v1, col);
        }
    });
}  // end if (m_ShowPhysicsColliders)
```

> **为什么不直接用 DrawCircle？**  
> `Renderer2D::DrawCircle` 渲染的是一个填充/描边的圆形 Quad（用于游戏内显示），  
> 它会写入 entityID、走 Framebuffer 混合等流程，不适合做调试覆盖层。  
> `DrawLine` 是专门用于调试可视化的轻量接口，不影响画面逻辑。

---

## 8. 运行流程图

```
编辑模式
│
├── Inspector 面板
│   └── 调整 Offset / Radius / 物理参数
│       └── 实时反映到覆盖层线框（无需重建物理）
│
└── 视口渲染（ForgeLayer::OnUpdate）
    └── Renderer2D::BeginScene(EditorCamera)
        ├── 相机线框绘制
        ├── [m_ShowPhysicsColliders == true]
        │   ├── BoxCollider2D → 4条线段矩形
        │   └── CircleCollider2D → 32段折线圆
        └── Renderer2D::EndScene()

按下 Play
│
├── Scene::Copy（复制所有组件到运行时场景）
│   └── 包含 CircleCollider2DComponent
│
└── OnRuntimeStart
    └── 遍历 RigidBody2DComponent
        └── 若有 CircleCollider2DComponent
            ├── 创建 b2CircleShape
            │   ├── m_p = Offset（圆心偏移）
            │   └── m_radius = max(Scale.x, Scale.y) * Radius
            └── CreateFixture → 与 Box2D 物理世界绑定

每帧（Play 中）
└── Box2D Step
    └── 读取 b2Body 位置/角度 → 同步回 TransformComponent
        └── 下一帧圆形渲染位置自动更新
```

---

## 9. 常见问题 & 注意事项

### Q1：按下 Play 后圆形不动？
**原因**：没有同时添加 `RigidBody2DComponent`。  
CircleCollider2DComponent 只定义形状，运动靠 Rigidbody。  
**解决**：同一 Entity 上同时添加 `Rigidbody 2D`（Type = Dynamic）和 `Circle Collider 2D`。

### Q2：Play 后圆形消失（渲染）？
**原因**：`Scene::Copy` 没有复制 `CircleRendererComponent`（圆形显示组件）。  
**解决**：已在 `CopyComponent` 列表中补全。

### Q3：圆形物理不响应碰撞但 Box 正常？
**原因**：`OnRuntimeStart` 只处理了 Box，没处理 Circle。  
**解决**：已在 `BoxCollider2D` 处理块之后紧接着添加 `CircleCollider2D` 处理块。

### Q4：实体缩放后碰撞范围不对？
**原因**：Box2D `b2CircleShape` 不支持非均匀缩放（椭圆）。  
当前方案取 `max(Scale.x, Scale.y)` 作为最终半径。  
若需要非均匀碰撞，需改用多个圆形或多边形近似。

### Q5：编辑器看不到线框？
检查菜单栏 **View → Physics Colliders** 是否勾选。  
线框只在 **Edit 模式**下显示，Play 模式不显示（调试专用）。

### Q6：Radius 最小值为什么是 0.001？
Box2D 内部对零半径会触发断言错误（assert）导致崩溃。  
Inspector 面板的 `DragFloat` 设置了 `min = 0.001f` 作为下限保护。
