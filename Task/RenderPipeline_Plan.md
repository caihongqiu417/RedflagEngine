# Redflag 真实渲染管线实施计划

## 总体目标

把 Redflag 的 3D 渲染从当前的「单 shader 纯色/贴图前向绘制」升级为
**混合 Deferred/Forward 真实渲染管线**，支持 PBR 材质、方向光阴影、
HDR + 后处理、编辑器调试视图。

## 管线选型

| 决策项 | 选定方案 | 理由 |
|--------|----------|------|
| 图形 API | OpenGL 4.5 | 现有抽象层完备，不必换 Vulkan |
| 材质模型 | Metallic-Roughness PBR | 与 glTF/Assimp 对接最自然 |
| 不透明 3D | Deferred（最终） | 多光源、SSAO、调试友好 |
| 透明/2D/Overlay | Forward | 简单且不需要 GBuffer |
| 阴影 | 先单张方向光 Shadow Map，后续 CSM | 最小可行 |
| 后处理 | HDR → Tonemap → Gamma → Bloom → FXAA | 逐步加入 |
| 编辑器 Picking | 保留独立 R32I attachment | 已验证稳定 |

## 架构层级

```
ForgeLayer（编辑器集成）
  └─ SceneRenderer（帧级调度）         ← 已完成骨架
       ├─ ShadowPass
       ├─ GeometryPass / MainForwardPass
       ├─ LightingPass
       ├─ TransparentPass
       ├─ EditorOverlayPass
       └─ PostProcessPass
            ├─ Renderer2D（2D 批处理执行器）
            └─ Renderer3D（3D 提交队列执行器）
```

---

## 阶段划分

### 阶段 0 — SceneRenderer 骨架 ✅ 已完成

- [x] 新增 `SceneRenderer.h / .cpp`
- [x] `Scene::OnUpdateEditor` → `SceneRenderer::RenderScene`
- [x] `Scene::OnUpdateRuntime` → `SceneRenderer::RenderRuntime`
- [x] 画面与功能与改动前完全一致

---

### 阶段 1 — Framebuffer 格式扩展

**目标**：让 OpenGLFramebuffer 能创建 HDR 和 Shadow 所需的纹理格式。

- [ ] `FramebufferTextureFormat` 枚举新增：
  - `RGBA16F`（HDR 颜色）
  - `RG16F`（法线压缩，可选）
  - `DEPTH32F`（可采样深度，用于 Shadow Map）
- [ ] `OpenGLFramebuffer::Invalidate` 中新增对应 case：
  - `RGBA16F` → `GL_RGBA16F / GL_RGBA / GL_FLOAT`
  - `DEPTH32F` → `GL_DEPTH_COMPONENT32F`，单独做 depth-only FBO
- [ ] `ClearAttachment` 按真实类型清除（当前一律按 GL_INT 清，对 float 不正确）
- [ ] `GetDepthAttachmentRendererID()` 新增接口，供 shader 采样 depth texture

**验证**：创建一个 RGBA16F FBO，绑定、清屏、Unbind，glCheckFramebufferStatus 无报错。

---

### 阶段 2 — 灯光系统基础

**目标**：场景中能添加灯光实体，数据能传入 shader。

- [x] 新增 `DirectionalLightComponent`：方向、颜色、强度、是否投影
- [x] 新增 `PointLightComponent`：位置、颜色、强度、半径
- [ ] `SceneRenderer` 收集阶段：遍历灯光组件，填充 `LightData` 数组
- [ ] 新增 `Light UBO`（binding = 3）：
  ```
  struct LightData {           // std140
      vec4 Direction;           // xyz = dir, w = type
      vec4 Color;               // xyz = color, w = intensity
      vec4 Position;            // xyz = pos, w = radius
      int  CastShadow;
      int  _pad[3];
  };
  // 上传：1 个方向光 + 最多 16 个点光
  ```
- [x] SceneHierarchyPanel 中可以添加/编辑灯光组件
- [x] 序列化/反序列化支持

**验证**：在面板中添加方向光，属性面板可编辑方向和颜色，不崩溃。

---

### 阶段 3 — PBR 材质升级

**目标**：Material 类支持 PBR 参数，Mesh 顶点带切线。

- [ ] `Material` 新增字段：
  - `BaseColorMap` + `BaseColorFactor`
  - `NormalMap`
  - `MetallicFactor` / `RoughnessFactor`
  - `MetallicRoughnessMap`（R=金属度 G=粗糙度）
  - `AOMap`（可选）
  - `EmissiveFactor`（可选，先留空）
  - `AlphaMode`（Opaque / Masked / Transparent）
  - `DoubleSided`
- [ ] `Material GPU UBO` 布局：
  ```
  struct MaterialGPU {         // std140, binding = 2
      vec4  BaseColor;
      float Metallic;
      float Roughness;
      float AO;
      int   UseBaseColorMap;
      int   UseNormalMap;
      int   UseMetallicRoughnessMap;
      int   _pad[2];
  };
  ```
- [ ] Mesh 顶点新增 `a_Tangent`（float3），Assimp 导入时计算切线
- [ ] MeshRendererComponent / ModelRendererComponent 中引用 PBR Material
- [ ] 属性面板中可编辑 PBR 参数并拖入贴图

**验证**：导入的模型在属性面板中显示 Metallic/Roughness 滑条，拖入 Normal Map 不崩溃。

---

### 阶段 4 — Forward PBR 主 Shader

**目标**：用一个前向 PBR shader 替换当前的 Renderer3D_Mesh.glsl，跑出真实感。

- [ ] 新建 `assets/shaders/PBR_Forward.glsl`
- [ ] Vertex shader：
  - 输入 `a_Position, a_Normal, a_Tangent, a_TexCoord`
  - 输出 `v_WorldPos, v_Normal, v_TexCoord, TBN matrix`
  - UBO: CameraData (binding=0), TransformData (binding=1)
- [ ] Fragment shader：
  - 采样 BaseColor / Normal / MetallicRoughness
  - TBN 变换法线
  - Cook-Torrance BRDF：GGX NDF + Smith Geometry + Fresnel Schlick
  - 遍历方向光 + 点光，累加直接光照
  - EntityID 输出到 `layout(location=1)`
  - HDR 输出到 `layout(location=0)`
- [ ] `SceneRenderer` 中替换 Renderer3D 使用的 shader
- [ ] 先不做阴影采样（下一步加）

**验证**：  
1. 导入一个球体模型，调节 Roughness 从 0→1，高光从锐利变模糊  
2. 调节 Metallic 从 0→1，非金属变金属外观  
3. 法线贴图使表面出现凹凸细节  
4. Entity picking 仍然正常  

---

### 阶段 5 — HDR Framebuffer + Tonemap

**目标**：场景渲染到 RGBA16F，再经后处理映射到 LDR 显示。

- [ ] SceneRenderer 创建 HDR FBO：`RGBA16F + RED_INTEGER + DEPTH24STENCIL8`
- [ ] 主渲染输出到 HDR FBO
- [ ] 新建 `assets/shaders/PostProcess_Tonemap.glsl`：
  - ACES Tonemap
  - Gamma 2.2 校正
  - 曝光控制 uniform
- [ ] 全屏 Quad 绘制到编辑器 viewport FBO（当前 ForgeLayer 的 m_Framebuffer）
- [ ] Picking attachment 从 HDR FBO blit 或共享到 viewport FBO

**验证**：  
1. 高亮区域不再死白一片  
2. 调节曝光值，画面整体亮度变化平滑  
3. Picking 仍然正常  

---

### 阶段 6 — Shadow Map（方向光）

**目标**：主方向光生成 shadow map，主渲染中采样。

- [ ] 新建 Shadow FBO：depth-only，例如 2048×2048
- [ ] 新建 `assets/shaders/Shadow_Depth.glsl`（只写深度）
- [ ] SceneRenderer 新增 ShadowPass：
  - 构造 light view-projection（正交投影）
  - 遍历 shadow casters 写 depth
- [ ] PBR_Forward.glsl 中增加：
  - Shadow UBO：`lightViewProjection`
  - 采样 shadow map，基础 PCF 2×2
- [ ] 编辑器暴露 shadow bias / shadow map size 参数

**验证**：  
1. 地面上出现模型阴影  
2. 旋转灯光方向，阴影跟随变化  
3. 无明显 peter-panning 或 shadow acne  

---

### 阶段 7 — Deferred Opaque

**目标**：不透明 3D 从 Forward 迁移到 Deferred。

- [ ] 新建 GBuffer FBO：
  - Attachment0: `RGBA8` — Albedo.rgb + AO
  - Attachment1: `RGBA16F` — Normal.xyz + Roughness
  - Attachment2: `RGBA8` — Metallic + Emission + Flags
  - Attachment3: `RED_INTEGER` — Entity ID
  - Depth: `DEPTH24STENCIL8`
- [ ] 新建 `assets/shaders/GBuffer_Write.glsl`（Geometry Pass shader）
- [ ] 新建 `assets/shaders/Deferred_Lighting.glsl`（全屏光照计算）
- [ ] SceneRenderer 拆分：
  - ShadowPass（不变）
  - GeometryPass → 不透明写 GBuffer
  - LightingPass → 全屏读 GBuffer + shadow map → HDR buffer
  - TransparentPass → 透明物体仍走 Forward
  - EditorOverlayPass → 2D + 线框 + Gizmo
  - PostProcessPass → Tonemap + Gamma
- [ ] 透明队列排序（back-to-front）

**验证**：  
1. 不透明模型视觉不变  
2. 多个点光下，draw call 不随灯光数增长  
3. 透明物体仍然正常  
4. 切换调试视图能看到 Albedo / Normal / Depth 各层  

---

### 阶段 8 — 后处理扩展

按此顺序逐个加入：

- [ ] Bloom（提取高亮 → 模糊 → 叠加）
- [ ] SSAO（采样 depth + normal → AO 贴图 → Lighting 中叠加）
- [ ] Color Grading（LUT 或参数化）
- [ ] Vignette
- [ ] FXAA

---

### 阶段 9 — 编辑器调试能力

- [ ] Viewport Debug Mode 下拉菜单，可切换查看：
  - Final / Albedo / Normal / Depth / Entity ID / Shadow Map / AO / Bloom
- [ ] Render Stats 面板（draw calls、顶点数、pass timing）
- [ ] Shader 热重载

---

### 阶段 10 — 优化

- [ ] Frustum Culling
- [ ] Opaque front-to-back 排序
- [ ] 状态切换最小化
- [ ] Instancing（相同 mesh + 相同 material 的实体合批）
- [ ] UBO/SSBO 批量上传

---

## 关键约束与注意事项

1. **Entity ID Picking 必须始终可用**  
   每个 pass 和 FBO 变更都要确认 R32I attachment 和 ReadPixel 不受影响。

2. **2D 和编辑器 Overlay 不进 GBuffer**  
   它们永远走 Forward，叠加在最终 HDR buffer 之上（PostProcess 之前）。

3. **阶段 4 完成后再进阶段 7**  
   先在 Forward 里验证 PBR 公式、法线空间、光照数据正确，再迁移到 Deferred。
   直接做 Deferred 调试成本高很多。

4. **ClearAttachment 修复优先**  
   当前 `OpenGLFramebuffer::ClearAttachment` 统一按 GL_INT 清，
   对 RGBA16F 等 float attachment 不正确，阶段 1 必须修。

5. **不要做过度抽象**  
   不搞 RenderGraph 图论系统。手动排列 pass，等 pass 多到维护痛时再抽象。

---

## 当前进度

- **阶段 0 ✅** — SceneRenderer 骨架已搭好
- **阶段 1 ✅** — Framebuffer 格式扩展完成
- **下一步 → 阶段 2** — 灯光系统基础
