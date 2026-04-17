# IBL 完整实现指南 — 从理论到代码

> 这不是一份"代码堆砌"文档，而是一份"思维路径"文档。
> 我会先告诉你**怎么把一个抽象公式一步步翻译成可运行的代码**，
> 然后才是整理好的完整代码。

---

## 第 0 章 — 先画路线图，再写代码

写这种复杂的东西，**最怕的是从第一行代码开始写**。
正确的做法是先回答 4 个问题：

### 4 个必须先回答的问题

1. **我最终要算的是什么？** (Output)
2. **我手上有什么输入？** (Input)
3. **从输入到输出，这个积分能不能提前算一部分？** (Precompute vs Runtime)
4. **预计算的结果要用什么数据结构存？** (Storage)

把这四个问题答出来，代码的骨架就自然浮现了。

### 对 IBL 的回答

| 问题 | 答案 |
|---|---|
| 最终要算的 | `L_o = ∫ f_r × L_i × cosθ dω`（环境光对某像素的出射辐射度）|
| 手上的输入 | 一张 HDR 等距柱状图（环境光强度的"原始数据"）|
| 能预计算什么 | Split Sum 说：积分里**只依赖环境的部分** + **只依赖 BRDF 的部分** 可以分开预计算 |
| 怎么存 | 3 张 GPU 纹理：`IrradianceMap`（Cube 32³）、`PrefilterMap`（Cube 128³ 带 5 mip）、`BRDFLUT`（2D 512²）|

这一步做完，你**已经知道程序长什么样了**：

```
Init:
    1. 读 HDR 图
    2. 算 IrradianceMap
    3. 算 PrefilterMap  
    4. 算 BRDFLUT
    5. 存起来

Render (每帧):
    1. 绑定 3 张贴图到纹理槽
    2. Shader 里做 3 次采样 + 乘加
```

---

## 第 1 章 — 把数学翻译成 GPU 可执行的步骤

课上讲义给我们的核心公式是：

$$
L_o(p, \omega_o) \approx \underbrace{\left(\frac{\int_\Omega L_i(p, \omega_i) d\omega_i}{\int_\Omega d\omega_i}\right)}_{\text{平均入射光强度}} \times \underbrace{\left(\int_\Omega f_r(p, \omega_i, \omega_o) \cos\theta_i d\omega_i\right)}_{\text{方向反射能力}}
$$

**左半** → 漫反射部分（和光线分布有关）
**右半** → 镜面反射部分（和 BRDF 有关）

### 翻译法则：每遇到一个积分号，问三件事

1. **积分的自变量是什么？**（例如 `ω_i`）
2. **被积函数依赖哪些变量？**（`L_i(ω_i)`、`f_r(ω_i, ω_o)`、`cosθ`）
3. **能不能把积分预先算出来，以"某个变量"为输入？**

套用到 IBL 上：

#### 漫反射部分 (Irradiance)
- 积分变量：ω_i（半球内所有方向）
- 结果依赖：**法线 N**（因为半球是以 N 为中心的）
- 预计算输入：**法线方向**
- 存储方式：**用法线作为 cubemap 采样方向** → `IrradianceMap[N]`

#### 镜面部分 (Split Sum)
这里比较巧妙——Epic 把它**再次**拆成两个积分：

$$
\int_\Omega f_r L_i \cos\theta d\omega \approx \underbrace{\int_\Omega L_i D(\omega_i, R) d\omega}_{\text{Prefiltered}} \times \underbrace{\int_\Omega \frac{f_r}{F} \cos\theta d\omega}_{\text{BRDF LUT}}
$$

- **Prefiltered 部分**：假设 V=R=N，只依赖 `(R, roughness)` → 存 `PrefilterMap[R]` + roughness 用 mip chain
- **BRDF LUT 部分**：Schlick 近似展开后只依赖 `(NdotV, roughness)` → 存 2D 查找表

**这就是为什么叫 Split Sum**：一个积分被拆成两个更简单的积分，两者的乘积 ≈ 原积分。

---

## 第 2 章 — 按顺序拆解流水线（5 步）

```
┌──────────┐  读文件  ┌──────────┐  投影  ┌──────────┐
│ HDR 图   │ ───────► │ 2D 纹理  │ ─────► │ EnvCube  │
│ (.hdr)   │          │ (RGB16F) │        │ (512³)   │
└──────────┘          └──────────┘        └────┬─────┘
                                               │
                    ┌──────────────────────────┼──────────────────────┐
                    ▼ Step 3                   ▼ Step 4               ▼ (天空盒背景)
            ┌───────────────┐         ┌────────────────┐
            │ IrradianceMap │         │ PrefilterMap   │
            │  (32³)        │         │ (128³, 5 mip)  │  Step 5 独立:
            │  漫反射 IBL    │         │  镜面 IBL       │    BRDF LUT (512², 2D)
            └───────────────┘         └────────────────┘
```

### 每一步的"输入/处理/输出"模板

我写这种多步流水线时，每一步都会先写一段注释：

```cpp
// Step N: <做什么>
// 输入: <需要哪些 GL 资源 / uniform>
// 处理: <核心 shader 逻辑一句话概括>
// 输出: <生成什么纹理，格式/尺寸/mip/wrap>
```

这样的好处是：**即使半年后回来看也能一眼抓住骨架**。

---

## 第 3 章 — 五个关键技巧（让你少踩坑）

### 技巧 1：用 cubemap 的"顶点局部坐标"当采样方向

这是我刚学时最不理解的地方，但一旦想通就永远忘不了：

> 把一个单位立方体放在原点，从中心往外看，
> **立方体某个 texel 的顶点坐标 (x,y,z)，本身就是这个 texel 对应的方向向量。**

所以顶点着色器几乎什么都不做：

```glsl
out vec3 v_LocalPos;
void main() {
    v_LocalPos = a_Pos;                     // ← 这就是采样方向
    gl_Position = u_Proj * u_View * vec4(a_Pos, 1.0);
}
```

Fragment 里用 `normalize(v_LocalPos)` 当做 N / R / 采样方向即可。

### 技巧 2：一个 FBO + 6 个 view 矩阵 渲染 cubemap

预计算要渲染到 cubemap 的 6 个面，不是开 6 个 FBO，而是**一个 FBO 循环 6 次**：

```cpp
for (int i = 0; i < 6; ++i) {
    glUniformMatrix4fv(viewLoc, 1, GL_FALSE, &s_Views[i][0][0]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, cubeTex, mipLevel);
    glClear(...);
    DrawCube();
}
```

六个 view 矩阵分别朝 ±X / ±Y / ±Z 看，**注意 +Y/-Y 的 up 要是 ±Z，不是常规的 Y**（cubemap 规范要求）。

### 技巧 3：mipmap 层级 = 不同 roughness 的预滤波结果

讲义里那张"绿色箭头采样很多次 ≈ 红色箭头采样一次"的图就是这个意思。

- roughness 越大 → GGX 分布越散 → 需要对环境贴图做越强的模糊
- 传统做法：运行时采多个样本
- Split Sum 做法：**把不同模糊程度的结果预先存在 mip chain 里**
  - mip 0 → roughness = 0.00
  - mip 1 → roughness = 0.25
  - mip 2 → roughness = 0.50
  - mip 3 → roughness = 0.75
  - mip 4 → roughness = 1.00
- 运行时一次 `textureLod(prefilter, R, roughness * 4)` 搞定

### 技巧 4：Filtered Importance Sampling 消除 firefly 噪点

GGX 重要性采样有个问题：PDF 稀疏的方向可能恰好命中 HDR 的"亮点"（太阳），导致一堆白色噪点。

解决：**每个样本根据其 PDF 计算等效立体角，再用 `0.5 * log2(saSample / saTexel)` 得到 mip 层级**，让稀疏样本去采"已经被 mip 平均过"的结果。

```glsl
float saTexel  = 4π / (6 * envMapSize²);
float pdf      = D * NdH / (4 * HdV);
float saSample = 1 / (N * pdf);
float mipLevel = 0.5 * log2(saSample / saTexel);
color += textureLod(envMap, L, mipLevel).rgb * NdL;
```

这是 Epic UE4 采用的方案，几乎无偏、效果立竿见影。

### 技巧 5：HDR 钳制，防止 inf 污染

**这是踩过坑才会记住的教训**：真实 HDR 文件里太阳位置的像素值可能是 `+Inf`。
- `glGenerateMipmap` 对 `inf` 求平均 → 产生 `NaN`
- `NaN` 通过 Irradiance 卷积、Prefilter 采样扩散到**整张贴图**
- 最终结果：模型出现奇怪的红/绿/蓝色块（甚至全黑）

**加载 HDR 后立刻钳制到 [0, 50]**，问题根除：

```cpp
for (int i = 0; i < w * h * ch; ++i) {
    float v = pixels[i];
    if (!std::isfinite(v)) v = 0.0f;
    else if (v > 50.0f)    v = 50.0f;
    else if (v < 0.0f)     v = 0.0f;
    pixels[i] = v;
}
```

---

## 第 4 章 — 代码架构（怎么组织文件）

### 三个文件 + 三个调用点

```
IBLPrecompute.h        ← 对外接口（Init / Shutdown / Bind / RenderSkybox）
IBLPrecompute.cpp      ← 预计算流水线 + 内嵌 shader
Renderer3D_Mesh.glsl   ← 运行时 PBR shader，包含 Split Sum ambient 分支
```

### 三个调用点

```cpp
SceneRenderer::Init()       → IBLPrecompute::Init("env.hdr");
Renderer3D::EndScene()      → IBLPrecompute::Bind();
SceneRenderer::RenderScene()→ IBLPrecompute::RenderSkybox(view, proj);
SceneRenderer::Shutdown()   → IBLPrecompute::Shutdown();
```

### 运行时数据流

```
主 PBR shader (binding 7/8/9)
        ↑
        └─ IBLPrecompute::Bind() 把 3 张预计算贴图绑到纹理槽
               ↑
               └─ 由 IBLPrecompute::Init() 生成
```

### 为什么不用引擎原生 Shader 抽象？

1. 预计算需要渲染到 cubemap 的 6 面 + 5 mip，现有 `OpenGLFramebuffer` 只支持 2D 颜色附件
2. 预计算 shader 只在 Init 时运行一次，不值得走 shaderc/SPIR-V 缓存
3. 内嵌 shader 源码避免文件依赖

**原则**：一次性工具代码用原生 API；长期热路径才上抽象层。

---

## 第 5 章 — 如何"从公式推到代码"的思维训练

### 举例：Irradiance 卷积

理论公式：

$$
E(N) = \int_\Omega L_i(\omega) \cdot \cos\theta \cdot d\omega
$$

#### 翻译步骤

**Step A. 选择坐标系**：
积分是在"以 N 为轴的半球"上做的。用球面坐标 (φ, θ) 最自然：
- φ ∈ [0, 2π)  绕 N 旋转一圈
- θ ∈ [0, π/2) 从 N 到赤道

**Step B. 把 dω 展开**：
立体角微元 `dω = sinθ dθ dφ`

**Step C. 离散化**：
把连续积分换成双循环求和。

**Step D. 坐标变换**：
球面坐标先拿到切线空间方向 `(sinθ cosφ, sinθ sinφ, cosθ)`，再用切线基（up/right/N）旋到世界空间采样 cubemap。

**Step E. 归一化**：
积分除以样本数，π 来自 `∫cosθ sinθ dθ dφ = π`。

#### 对照最终代码

```glsl
for(float phi = 0; phi < 2π; phi += delta)
  for(float theta = 0; theta < π/2; theta += delta)
  {
      vec3 ts = vec3(sinθ·cosφ, sinθ·sinφ, cosθ);        // Step A+B+D
      vec3 w  = ts.x*right + ts.y*up + ts.z*N;            // Step D
      irr += texture(u_EnvMap, w).rgb * cos(θ) * sin(θ);  // Step B+C
      count++;
  }
irr = π * irr / count;                                     // Step E
```

**每一行代码都能对应到一步数学推导**。这就是我说的"逻辑清晰"——写代码前你已经在纸上把每一步想清楚了。

---

## 第 6 章 — 一个合格程序员的 Debug 流程

你这次 IBL 实现遇到了两个经典 bug，我们的定位过程就是教科书级的 debug 示范：

### Bug 1：模型全黑
症状：加了 IBL 之后模型反而变黑。

**错误思路**：立刻去看 shader 代码找 bug。
**正确思路**：先确认 IBL 的 3 张贴图**本身**是对的。

**定位方法**：在 shader 里加 `DEBUG_IBL` 宏，一次只显示一张贴图，视觉验证。

```glsl
#define DEBUG_IBL 1   // 直接涂 IrradianceMap
#define DEBUG_IBL 2   // 直接涂 PrefilterMap
#define DEBUG_IBL 3   // 直接涂 BRDFLUT
```

**诊断结果**：三张都正确 → 问题不在预计算，在最终合成。

### Bug 2：加了倍率放大后出现红/绿/蓝色块
**错误思路**：继续改参数看能不能调亮。
**正确思路**：红/绿/蓝色块是 NaN 的征兆，把不同中间变量的 `isnan` 映射到不同颜色定位来源。

```glsl
if (badDiff) ambient = vec3(1,0,0);   // Irradiance 采样 → NaN
if (badSpec) ambient = vec3(0,1,0);   // Prefilter 采样 → NaN
```

**诊断结果**：漫反射 + 镜面都中招 → 两张贴图都污染 → 源头在更上游（HDR 的 inf → mipmap → NaN）。
**修复**：加载 HDR 后钳制 `[0, 50]`。

### 教训提炼

> Debug 最重要的能力是**缩小搜索范围**。
> 每次加诊断，都要问："这个诊断能把可能性减半吗？"

---

## 第 7 章 — 完整代码

### 7.1 `IBLPrecompute.h` — 对外接口

```cpp
#pragma once
#include <cstdint>
#include <string>
#include <glm/glm.hpp>

namespace Redflag {

    struct IBLTextures {
        uint32_t EnvCubemap    = 0;
        uint32_t IrradianceMap = 0;
        uint32_t PrefilterMap  = 0;
        uint32_t BRDFLUT       = 0;
    };

    class IBLPrecompute {
    public:
        static void Init(const std::string& hdrPath);
        static void Shutdown();
        static void Bind();                                                  // 绑定到 slot 7/8/9
        static void RenderSkybox(const glm::mat4& view, const glm::mat4& projection);
        static bool IsReady();
        static const IBLTextures& GetTextures();
    };

}
```

### 7.2 `IBLPrecompute.cpp` — 预计算流水线

完整代码见 `Redflag/src/Redflag/Renderer/IBLPrecompute.cpp`。
关键片段 / 核心流程：

#### 流水线入口

```cpp
void IBLPrecompute::Init(const std::string& hdrPath)
{
    // 0. 保存 GL 状态
    GLint prevFBO, prevVP[4];
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    glGetIntegerv(GL_VIEWPORT, prevVP);

    // 1. 辅助几何 & FBO
    MakeCubeVAO();
    MakeQuadVAO();
    glGenFramebuffers(1, &s_CaptureFBO);
    glGenRenderbuffers(1, &s_CaptureRBO);
    // ... 绑定 & 分配深度存储

    // 2. 加载 HDR
    float* pixels = stbi_loadf(hdrPath.c_str(), &w, &h, &ch, 0);
    // 钳制 inf/NaN（关键！）
    for (int i = 0; i < w*h*ch; ++i) {
        float v = pixels[i];
        if (!std::isfinite(v)) v = 0.0f;
        else if (v > 50.0f)    v = 50.0f;
        else if (v < 0.0f)     v = 0.0f;
        pixels[i] = v;
    }
    // 上传为 GL_RGB16F 2D 纹理

    // 3. Equirect → EnvCubemap (512³)
    //    注意：先用 GL_LINEAR，渲染完再 glGenerateMipmap，
    //    最后才切到 GL_LINEAR_MIPMAP_LINEAR

    // 4. Irradiance 卷积 (32³)
    //    用 EnvCubemap 作为源，采样 2500 次 / 像素

    // 5. Prefilter (128³, 5 mip)
    //    每个 mip 对应一个 roughness，Filtered Importance Sampling

    // 6. BRDF LUT (512², RG16F, 2D 查找表)

    // 7. 编译天空盒 shader（保留到 Shutdown）

    // 8. 清理临时资源 & 恢复 GL 状态
}
```

#### Irradiance Fragment Shader

```glsl
#version 450 core
in vec3 v_LocalPos;
out vec4 FragColor;
uniform samplerCube u_EnvMap;
const float PI = 3.14159265359;

void main() {
    vec3 N = normalize(v_LocalPos);
    vec3 irr = vec3(0.0);

    vec3 up    = vec3(0, 1, 0);
    vec3 right = normalize(cross(up, N));
    up         = normalize(cross(N, right));

    float delta = 0.025;
    float count = 0.0;
    for(float phi = 0.0; phi < 2.0*PI; phi += delta)
    for(float theta = 0.0; theta < 0.5*PI; theta += delta) {
        vec3 ts = vec3(sin(theta)*cos(phi), sin(theta)*sin(phi), cos(theta));
        vec3 w  = ts.x*right + ts.y*up + ts.z*N;
        irr += texture(u_EnvMap, w).rgb * cos(theta) * sin(theta);
        count++;
    }
    irr = PI * irr / count;
    FragColor = vec4(irr, 1.0);
}
```

#### Prefilter Fragment Shader（Filtered Importance Sampling）

```glsl
#version 450 core
in  vec3 v_LocalPos;
out vec4 FragColor;
uniform samplerCube u_EnvMap;
uniform float u_Roughness;
uniform float u_EnvMapSize;      // 源 cubemap 面分辨率 (512)

const float PI = 3.14159265359;
const uint  SAMPLES = 1024u;

// VdC + Hammersley + SampleGGX + DistributionGGX ... (略)

void main() {
    vec3 N = normalize(v_LocalPos);
    vec3 R = N;  vec3 V = R;                  // Split Sum 假设 V=R=N

    vec3  color  = vec3(0.0);
    float totalW = 0.0;
    float saTexel = 4.0 * PI / (6.0 * u_EnvMapSize * u_EnvMapSize);

    for(uint i = 0u; i < SAMPLES; ++i) {
        vec2 Xi = Hammersley(i, SAMPLES);
        vec3 H  = SampleGGX(Xi, N, u_Roughness);
        vec3 L  = normalize(2.0*dot(V,H)*H - V);
        float NdL = max(dot(N,L), 0.0);
        if(NdL > 0.0) {
            // Filtered Importance Sampling：按 PDF 选 mip
            float NdH = max(dot(N,H), 0.0);
            float HdV = max(dot(H,V), 0.0);
            float D   = DistributionGGX(NdH, u_Roughness);
            float pdf = (D * NdH / (4.0 * HdV)) + 0.0001;
            float saSample = 1.0 / (float(SAMPLES) * pdf + 0.0001);
            float mipLevel = u_Roughness == 0.0
                           ? 0.0
                           : 0.5 * log2(saSample / saTexel);

            color  += textureLod(u_EnvMap, L, mipLevel).rgb * NdL;
            totalW += NdL;
        }
    }
    FragColor = vec4(color / totalW, 1.0);
}
```

#### BRDF LUT Fragment Shader

```glsl
#version 450 core
in  vec2 v_UV;
out vec4 FragColor;
const float PI = 3.14159265359;
const uint  SAMPLES = 1024u;

// VdC + Hammersley + SampleGGX + IBL 几何项 G1/GSmith (k = α²/2) ... (略)

vec2 IntegrateBRDF(float NdV, float rough) {
    vec3 V = vec3(sqrt(1.0 - NdV*NdV), 0.0, NdV);
    vec3 N = vec3(0, 0, 1);
    float A = 0.0, B = 0.0;
    for(uint i = 0u; i < SAMPLES; ++i) {
        vec2 Xi = Hammersley(i, SAMPLES);
        vec3 H  = SampleGGX(Xi, N, rough);
        vec3 L  = normalize(2.0*dot(V,H)*H - V);
        float NdL = max(L.z, 0.0);
        float NdH = max(H.z, 0.0);
        float VdH = max(dot(V,H), 0.0);
        if(NdL > 0.0) {
            float G     = GSmith(N, V, L, rough);
            float G_Vis = (G * VdH) / (NdH * NdV);
            float Fc    = pow(1.0 - VdH, 5.0);
            A += (1.0 - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }
    return vec2(A, B) / float(SAMPLES);
}

void main() {
    FragColor = vec4(IntegrateBRDF(v_UV.x, v_UV.y), 0.0, 1.0);
}
```

#### 天空盒 Shader

```glsl
// Vertex
#version 450 core
layout(location=0) in vec3 a_Pos;
uniform mat4 u_View;           // 已去平移
uniform mat4 u_Projection;
out vec3 v_LocalPos;
void main() {
    v_LocalPos = a_Pos;
    vec4 clip  = u_Projection * u_View * vec4(a_Pos, 1.0);
    gl_Position = clip.xyww;   // z = w → NDC.z = 1.0
}

// Fragment
#version 450 core
in  vec3 v_LocalPos;
out vec4 FragColor;
uniform samplerCube u_EnvMap;
void main() {
    vec3 color = texture(u_EnvMap, normalize(v_LocalPos)).rgb;
    color = color / (color + vec3(1.0));           // Reinhard
    color = pow(color, vec3(1.0/2.2));              // Gamma
    FragColor = vec4(color, 1.0);
}
```

### 7.3 `Renderer3D_Mesh.glsl` — 主 PBR shader 的 Split Sum 分支

```glsl
// ---- IBL 纹理声明（与 C++ 端 Bind() 对应）----
layout(binding = 7) uniform samplerCube u_IrradianceMap;
layout(binding = 8) uniform samplerCube u_PrefilterMap;
layout(binding = 9) uniform sampler2D   u_BRDFLUT;

// ---- Fresnel with roughness (IBL 专用版) ----
vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0)
             * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// ---- 在 main() 里组合 Split Sum ----
vec3 ambient;
if (u_ViewPosition.w > 0.5) {                  // IBL 就绪标志
    vec3 kS = FresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
    vec3 kD = (1.0 - kS) * (1.0 - metallic);

    // 漫反射 IBL
    vec3 irradiance = texture(u_IrradianceMap, N).rgb;
    vec3 diffuseIBL = irradiance * albedo;

    // 镜面 IBL (Split Sum)
    vec3 R = reflect(-V, N);
    const float MAX_REFLECTION_LOD = 4.0;
    vec3 prefiltered = textureLod(u_PrefilterMap, R, roughness * MAX_REFLECTION_LOD).rgb;
    vec2 brdf        = texture(u_BRDFLUT, vec2(max(dot(N,V),0.0), roughness)).rg;
    vec3 specularIBL = prefiltered * (kS * brdf.x + brdf.y);

    ambient = (kD * diffuseIBL + specularIBL) * ao;
} else {
    ambient = vec3(0.03) * albedo * ao;         // 回退
}

vec3 color = ambient + Lo;
// ... tone map + gamma
```

### 7.4 集成点

```cpp
// SceneRenderer.cpp
void SceneRenderer::Init() {
    // ... 其它初始化
    IBLPrecompute::Init("assets/textures/environment.hdr");
}

void SceneRenderer::RenderScene(Scene& scene, const EditorCamera& camera) {
    // ... 2D 渲染
    Renderer3D::BeginScene(camera);
    // ... 提交 mesh / model
    Renderer3D::EndScene();
    IBLPrecompute::RenderSkybox(camera.GetViewMatrix(), camera.GetProjection());
}

void SceneRenderer::Shutdown() {
    IBLPrecompute::Shutdown();
}

// Renderer3D.cpp — BeginScene 里设置 IBL 就绪标志
s_Data.CameraBuffer.ViewPosition = glm::vec4(cameraPos,
    IBLPrecompute::IsReady() ? 1.0f : 0.0f);

// EndScene 里绑定 IBL 贴图
s_Data.MeshShader->Bind();
IBLPrecompute::Bind();
// ... 发出 draw call
```

---

## 第 8 章 — 一条总结建议

> **复杂代码不是"写出来"的，是"推出来"的。**

写 IBL 这种代码前，我脑中反复出现的画面是：

1. 一张纸，画三个方框分别标 `Irradiance / Prefilter / BRDF`
2. 每个方框写清楚它的**输入是什么、输出是什么、依赖哪个公式**
3. 方框之间用箭头连：EnvCubemap 是 Irradiance 和 Prefilter 的共同输入，BRDF LUT 独立
4. 每个方框扩展成一段伪代码，伪代码再变成 GLSL

这个过程就是**把"一个公式"切成"多个可以独立测试的模块"**。
当每个模块都能独立验证（见第 6 章的 DEBUG_IBL），整合时的 bug 就只可能在"模块之间的接口"。

---

## 附：常见陷阱清单

| 陷阱 | 症状 | 修复 |
|---|---|---|
| HDR 含 `+Inf` | 模型出现 NaN 色块 | 加载后钳制 `[0, 50]` |
| Cubemap 预设 `LINEAR_MIPMAP_LINEAR` 但未 generateMipmap | 纹理不完整 → 采样返回黑 | 先 `LINEAR`，mipmap 生成后再切 |
| Prefilter firefly 噪点 | 高 roughness 出现亮斑 | Filtered Importance Sampling |
| 天空盒 `xyww` 技巧 + 正交投影 | 天空盒消失/错乱 | 检测 `projection[3][3] > 0.5` 跳过 |
| Cubemap `+Y/-Y` 面 up 用错 | 顶/底面渲染颠倒 | up 必须是 `±Z`（OpenGL cubemap 规范）|
| 预计算完删了 cubeVAO | 天空盒后续无法渲染 | VAO 推迟到 Shutdown 释放 |
| View 矩阵未去平移 | 相机移动时穿出天空盒 | `mat4(mat3(view))` |
| IBL ready 标志没传 | shader 走错分支 | `u_ViewPosition.w` 编码 ready flag |

---

*End of Guide*
