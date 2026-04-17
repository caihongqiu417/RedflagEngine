# 实时阴影完整实现指南

> **阅读对象**：刚入门图形学，知道什么是顶点/片元着色器，但没写过阴影的程序员。
>
> **阅读方式**：从头到尾按顺序读。每个术语在第一次出现时都会解释。
>
> **本引擎的实现**：方向光 Shadow Mapping + PCF + PCSS，基于 OpenGL 4.5。

---

# 第一部分：阴影的原理

---

## 1. 为什么阴影不是"自动"有的

初学者常见困惑：场景里有灯光、有物体，为什么还要专门写代码才有阴影？

答案：GPU 的渲染方式是"一个像素一个像素"地画。画某个像素时，GPU 只知道这个像素自己的位置和法线，**不知道前面是否有别的物体挡住了光**。

直接光照公式（Lambert / Cook-Torrance）算的是"假设这个点能完全看到光源"的亮度。要判断"光是否被挡住了"，需要**额外的信息**。

---

## 2. Shadow Mapping 的核心思路

Shadow Mapping 是目前实时渲染最主流的阴影算法。思路非常简单：

**站在光源的位置看一遍场景，把看到的每个像素的"深度"（离光源的距离）记录到一张贴图里。**

然后正式渲染时，对于每个像素：
1. 把它的位置变换到"光源看过去"的坐标系
2. 在深度贴图里查到"光源在这个方向上最近看到的东西有多远"
3. 比较：
   - 如果这个像素比记录的最近距离**更远** → 它在某个物体后面 → **被遮挡（阴影中）**
   - 如果一样远（或更近） → 它就是光源看到的最近物体 → **被照亮**

就这么简单。整个算法只是"看两遍场景"。

---

## 3. 两遍渲染

| 阶段 | 做什么 | 输出 |
|---|---|---|
| **Shadow Pass** | 从光源视角渲染整个场景，只记录深度 | 一张深度贴图（Shadow Map） |
| **Main Pass** | 从相机视角正常渲染，同时查 Shadow Map 判断是否被遮挡 | 最终画面 |

Shadow Pass **不画颜色**，只写深度。所以 Fragment Shader 可以是空的——OpenGL 的固定管线会自动把 `gl_FragCoord.z` 写入深度缓冲。

---

## 4. 光源空间（Light Space）

要从光源的角度"看"场景，需要一个**光源空间的变换矩阵**。

### 4.1 什么是空间变换

一个 3D 点可以从不同的"角度"来描述：
- **世界空间**：点在场景里的绝对位置
- **相机空间（View Space）**：从相机出发看，这个点在哪个方向、多远
- **裁剪空间（Clip Space）**：经过投影变换后，准备送给 GPU 做裁剪和光栅化

Shadow Mapping 里我们多了一个：
- **光源空间**：从光源出发看，这个点在哪个方向、多远

### 4.2 方向光的"相机"

方向光（比如太阳）没有位置，只有方向。我们需要"假装"太阳有一个位置来构建观察矩阵。

做法：
- **View 矩阵**：`lookAt(-lightDir * 30, vec3(0), up)` —— 把"虚拟光源位置"放在原点沿光线反方向 30 单位处，朝原点看
- **Projection 矩阵**：因为方向光的光线是平行的（不像点光源那样发散），用**正交投影**（Orthographic Projection），不用透视投影

正交投影不产生"近大远小"的效果，所有平行光线保持平行——刚好符合方向光的物理特性。

```cpp
// “假装”太阳有个位置：沿光线反方向退 30 个单位，朝原点看
glm::mat4 lightView = glm::lookAt(
    -lightDirection * 30.0f,    // 虚拟光源位置（立于原点反方向）
    glm::vec3(0.0f),            // 看向原点（场景中心）
    glm::vec3(0, 1, 0)          // 上方向
);
float half = orthoHalfSize;     // 视锥的半宽（可调，控制阴影覆盖范围）
// 正交投影：平行光线不需要透视，所有方向保持平行
glm::mat4 lightProj = glm::ortho(
    -half, half,                // 左右边界
    -half, half,                // 上下边界
    -50.0f, 50.0f               // 近远平面（足够包含场景深度范围）
);
// 最终矩阵 = 投影 × 观察，把世界坐标变换到光源裁剪空间
glm::mat4 lightSpaceMatrix = lightProj * lightView;
```

**`orthoHalfSize` 怎么选**：
- 太小 → 场景边缘没有阴影
- 太大 → Shadow Map 像素被摊开到更大面积，阴影变糊
- 本引擎默认 25.0，编辑器里可调

### 4.3 Light Space Matrix 的含义

`lightSpaceMatrix` 做的事是：**把一个世界空间的 3D 点变换到光源的裁剪空间（NDC）**。

变换后坐标的含义：
- `xy`：这个点在 Shadow Map 上的 UV 坐标（经过 `*0.5+0.5` 转换后）
- `z`：这个点离光源有多远（0=最近面，1=最远面）

---

## 5. Shadow Map 的创建

Shadow Map 就是一张只有深度的 Framebuffer：

```cpp
constexpr uint32_t SHADOW_MAP_SIZE = 2048;  // 分辨率（2048×2048 像素）

FramebufferSpecification spec;
spec.Width  = SHADOW_MAP_SIZE;              // 宽 = 2048
spec.Height = SHADOW_MAP_SIZE;              // 高 = 2048
spec.Attachments = { FramebufferTextureFormat::DEPTH32F };  // 只要深度附件，32位浮点精度
// 不需要颜色附件！Shadow Pass 只写深度
```

**为什么不用颜色附件**：Shadow Pass 只需要深度，不需要颜色。GPU 自动把每个片元的深度写入这张纹理。

**为什么用 32 位浮点（DEPTH32F）**：深度比较时精度很重要。16 位或 24 位在远处容易出现"条纹状"瑕疵（z-fighting）。

**分辨率 2048 怎么选**：
- 越大 → 阴影越清晰，但 GPU 存储和带宽开销越大
- 2048 是实时渲染中常见的平衡点
- 4096 适合高质量需求

---

## 6. Shadow Pass — 从光源视角渲染深度

### 6.1 Vertex Shader

```glsl
#version 450 core
layout(location = 0) in vec3 a_Position;    // 模型空间的顶点位置

// Camera UBO (binding = 0)——和主 Pass 共用同一个 UBO
layout(std140, binding = 0) uniform Camera {
    mat4 u_ViewProjection;                  // 相机 VP（Shadow Pass 不用）
    vec4 u_ViewPosition;                    // 相机位置（Shadow Pass 不用）
    mat4 u_LightSpaceMatrix;                // ← 上面构造的光源空间矩阵
    // ...
};
uniform mat4 u_Transform;                   // 物体自己的模型矩阵（位置/旋转/缩放）

void main() {
    // 模型空间 → 世界空间（u_Transform）→ 光源裁剪空间（u_LightSpaceMatrix）
    gl_Position = u_LightSpaceMatrix * u_Transform * vec4(a_Position, 1.0);
}
```

这一步做的事：把顶点从模型空间 → 世界空间（u_Transform）→ 光源裁剪空间（u_LightSpaceMatrix）。

### 6.2 Fragment Shader

```glsl
#version 450 core
void main() {
    // 空！深度由 OpenGL 固定管线自动写入 gl_FragDepth
    // 我们不需要输出任何颜色，Shadow Map 只要深度
}
```

"什么都不做"的 Fragment Shader 恰好是我们要的——只记录深度。

### 6.3 渲染流程（C++ 端）

```cpp
static void RunShadowPass(const glm::mat4& lightSpaceMatrix)
{
    // 1. 保存当前 FBO 和 viewport（待会要还原）
    // 2. 绑定 Shadow FBO（只有深度附件，无颜色）
    s_ShadowFBO->Bind();
    glViewport(0, 0, SHADOW_MAP_SIZE, SHADOW_MAP_SIZE); // viewport 设为 Shadow Map 尺寸
    glClear(GL_DEPTH_BUFFER_BIT);                       // 清空深度缓冲

    // 3. 设置 LightSpaceMatrix 到 UBO（让 VS 能读到）
    // 4. 遍历所有不透明物体，只用 ShadowDepth shader 画一遍
    for (auto& cmd : drawCommands) {
        if (cmd.isTransparent) continue;    // 跳过半透明物体（它们的阴影处理太复杂）
        // 上传模型矩阵 u_Transform → DrawIndexed（只写深度）
    }

    // 5. 还原之前的 FBO 和 viewport（切回主渲染目标）
}
```

**为什么跳过半透明物体**：半透明物体的阴影很复杂（需要考虑透光率），简单 Shadow Mapping 处理不了。通常直接忽略。

---

## 7. Main Pass — 查 Shadow Map 判断遮挡

### 7.1 把片元位置变换到光源空间

在主 Shader 的**顶点阶段**，把世界坐标变换到光源裁剪空间：

```glsl
// 顶点着色器：把世界坐标变换到光源裁剪空间（输出给片元着色器插值）
v_LightSpacePos = u_LightSpaceMatrix * vec4(worldPosition, 1.0);
```

在**片元阶段**，做透视除法并映射到 [0, 1]：

```glsl
// 透视除法：齐次坐标 (x,y,z,w) → NDC (x/w, y/w, z/w)
vec3 proj = v_LightSpacePos.xyz / v_LightSpacePos.w;
// NDC 范围是 [-1,1]，变换到 [0,1] 才能用作 UV 和深度比较
proj = proj * 0.5 + 0.5;
// proj.xy = 在 Shadow Map 上的 UV 坐标
// proj.z  = 这个片元到光源的深度，用于和 Shadow Map 比较
```

> **透视除法（Perspective Division）**：GPU 裁剪空间的坐标是齐次坐标 (x, y, z, w)，除以 w 得到归一化设备坐标（NDC）。对正交投影来说 w 总是 1，除不除没区别，但代码上保持一致。

### 7.2 深度比较

```glsl
// 从 Shadow Map 查询该 UV 处记录的最近深度
float closestDepth = texture(u_ShadowMap, proj.xy).r;  // r 通道 = 深度值
float currentDepth = proj.z;                             // 当前片元到光源的深度

// 简单比较：当前深度 > 记录的最近深度 → 前面有东西挡着 → 在阴影中
float shadow = currentDepth > closestDepth ? 0.0 : 1.0;
// 0.0 = 在阴影中（被遮挡）
// 1.0 = 被照亮（未被遮挡）
```

**直觉**：如果当前片元比 Shadow Map 里记录的"最近物体"更远，说明前面有东西挡着 → 在阴影中。

### 7.3 超出 Shadow Map 范围的处理

```glsl
// 超出光源视锥范围的片元，默认“不在阴影中”
// 否则场景边缘会莫名其妙变黑
if (proj.z > 1.0 || proj.z < 0.0)      return 1.0;  // 超出光源近远平面
if (proj.xy 超出 [0,1])                 return 1.0;  // 超出 Shadow Map UV 边界
```

这些情况下默认"不在阴影中"。否则会出现场景边缘莫名其妙的黑色。

---

# 第二部分：阴影瑕疵与解决方案

---

## 8. Shadow Acne（阴影粉刺）

### 8.1 问题

最基础的深度比较会导致**整个被光照射的表面上出现条纹状的阴暗交替**。这叫 Shadow Acne。

### 8.2 为什么会出现

Shadow Map 的每个像素覆盖的是一小片区域（不是无穷小的点）。当一个表面斜着被照射时，Shadow Map 上一个像素记录的深度，其实是这一小片区域的 **某一个点** 的深度，但在主渲染中，这片区域内的其他点可能比这个记录值稍微远一点或近一点。

结果：表面的一部分像素判定为"比记录更远" → 被当成阴影 → 出现条纹。

### 8.3 解决：Depth Bias（深度偏移）

思路：比较前，给当前片元的深度**减去一个小小的偏移量**，容忍小范围的深度差异。

```glsl
// 深度偏移：光线越斜打（NdotL 越小），需要越大的 bias
// max() 保证 bias 至少有 depthBias·0.1，避免正对时 bias=0
float bias = max(depthBias * (1.0 - NdotL), depthBias * 0.1);
// 将当前深度减去 bias，容忍小范围的深度差异
float refDepth = proj.z - bias;
// 用 refDepth 代替原始深度做比较
float shadow = refDepth > closestDepth ? 0.0 : 1.0;
```

**为什么 bias 和 `NdotL` 有关**：
- `NdotL` = `dot(N, L)` = 光线和法线的余弦
- 光线越斜 → `NdotL` 越小 → Shadow Map 像素横跨的深度范围越大 → 需要更大的 bias

**搭配 Normal Bias**：除了深度方向偏移，还可以沿法线方向往外推一小段，进一步减少 acne：

```glsl
// Normal Bias：沿法线方向往外推一小段，进一步减少 acne
// texelSize = 1/ShadowMapSize，保证偏移量与 Shadow Map 精度成比例
bias += normalBias * texelSize;
```

**过大的 bias 会导致什么**：阴影整体"脱离"物体，特别是脚底下会出现一条亮缝。这叫 **Peter Panning**（像彼得·潘的漂浮影子）。所以 bias 需要调到"没有 acne、也没有脱离"的平衡点。

本引擎的默认值：
- `depthBias = 0.0015`
- `normalBias = 1.0`

---

## 9. 硬阴影太假了

即使解决了 acne，Shadow Mapping 产生的阴影边缘是**完全锐利的**（一个像素是 0，旁边可能直接是 1）。这叫**硬阴影**。

现实中，面积光源（太阳不是一个点）会产生**软阴影**——阴影边缘有逐渐过渡的半影区域。

解决方案按复杂度排列：
1. **PCF**（最简单）—— 用周围多个点的平均值模糊阴影边缘
2. **PCSS**（更真实）—— 遮挡物越远，阴影越软

---

# 第三部分：PCF — 用多次采样来柔化阴影

---

## 10. PCF 的原理

**PCF** = Percentage-Closer Filtering（百分比接近过滤）。

不是对深度值做模糊（那样做结果是错的），而是：**在 Shadow Map 上对当前点周围多个位置做深度比较，把比较结果（0 或 1）取平均**。

差别在于：
- 错误做法：先把周围深度值平均，再和当前深度比 → 深度的平均值没有物理意义
- 正确做法（PCF）：每个周围位置**独立做比较**，再把 0/1 结果平均 → 得到 [0, 1] 之间的遮挡比例

### 10.1 3×3 PCF

在当前 UV 周围，取 3×3 = 9 个点，每个点独立做深度比较，结果除以 9：

```glsl
// 3×3 PCF：在当前 UV 周围 9 个点做独立深度比较，结果取平均
// 输入：baseUV = Shadow Map UV，refDepth = 已减 bias 的当前深度，texelSize = 1/分辨率
// 输出：[0,1] 遮挡比例，0=完全阴影，1=完全照亮
float PCF3x3(vec2 baseUV, float refDepth, float texelSize) {
    float sum = 0.0;
    // 遍历 3×3 = 9 个采样点（以当前像素为中心，向四周扩展 1 个像素）
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 offset = vec2(x, y) * texelSize;               // UV 偏移量
            float depth = texture(u_ShadowMap, baseUV + offset).r; // 采样该点深度
            sum += (refDepth <= depth) ? 1.0 : 0.0;             // 独立比较：“是否被照亮”
        }
    }
    return sum / 9.0;  // 9 个结果取平均 → 光滑过渡
}
```

**`texelSize`** = `1.0 / ShadowMapSize`。对 2048 的 Shadow Map 来说就是 `1.0 / 2048 ≈ 0.000488`。这是在 UV 空间里"一个像素的宽度"。

**效果**：阴影边缘不再是 0/1 跳变，而是 0 → 0.22 → 0.56 → 0.89 → 1 这样的过渡。

### 10.2 可变半径 PCF

3×3 的柔化范围固定。如果想要更软的边缘，可以增大采样核：

```glsl
// 可变半径 PCF：5×5 采样核，通过 radius 控制扩展范围
// radius = 1.0 时等价于 3×3，radius = 3.0 时采样范围扩大 3 倍
float PCFVariable(vec2 baseUV, float refDepth, float radius, float texelSize) {
    float sum = 0.0;
    float samples = 0.0;                    // 采样计数
    // 5×5 采样网格
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            // UV 偏移 = 网格坐标 × 半径系数 × 纹素尺寸
            vec2 offset = vec2(x, y) * (radius * 0.5) * texelSize;
            sum     += SampleShadow(baseUV + offset, refDepth); // 单点深度比较
            samples += 1.0;
        }
    }
    return sum / samples;                   // 平均過挡比例
}
```

- `radius = 1.0` 时等价于 3×3
- `radius = 3.0` 时采样范围变大 3 倍，阴影更柔和
- 代价：更多纹理采样 = 更多 GPU 开销

本引擎的编辑器里可以拖动 `PCF Radius` 来调整。

---

# 第四部分：PCSS — 让阴影距离越远越模糊

---

## 11. 为什么 PCF 还不够真实

PCF 的阴影**各处柔化程度一样**。但现实中：
- 手指紧贴桌面时，指影非常清晰
- 手举高一些，指影就明显变模糊了

真实的半影宽度和**遮挡物到接收面的距离**成正比。

**PCSS**（Percentage-Closer Soft Shadows）就是为了模拟这个效果。

---

## 12. PCSS 的三步流程

### Step 1：Blocker Search（搜索遮挡物）

在 Shadow Map 上当前 UV 的周围搜索一片区域，找到所有**比当前片元更近（即挡在光和当前片元之间）**的像素，算这些"遮挡像素"的**平均深度**。

```glsl
// Blocker Search：在 Shadow Map 上搜索“遮挡者”并算平均深度
// 遮挡者 = 比当前片元更近光源的像素（即挡在光和当前点之间的物体）
float FindBlockerDepth(vec2 baseUV, float refDepth, float searchRadius, float texelSize) {
    float blockerSum = 0.0;                 // 遮挡者深度累加
    float count      = 0.0;                 // 遮挡者计数

    // 5×5 搜索网格
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            // 搜索范围由 searchRadius 控制
            vec2 offset = vec2(x, y) * (searchRadius / 2.0) * texelSize;
            float depth = texture(u_ShadowMap, baseUV + offset).r; // 采样该点深度
            if (depth < refDepth) {         // 这个采样点比当前片元更近 → 是一个“遮挡者”
                blockerSum += depth;        // 累加遮挡者深度
                count += 1.0;               // 计数 +1
            }
        }
    }
    if (count < 1.0) return -1.0;           // 没有遮挡者 → 不在阴影中，返回 -1 标记
    return blockerSum / count;              // 返回遮挡者的平均深度
}
```

### Step 2：估算半影宽度（Penumbra Estimation）

用遮挡物的平均深度和当前片元深度的差来估算半影宽度：

```glsl
// 半影宽度估算：(currentDepth - avgBlockerDepth) 越大 → 遮挡物离得越远 → 阴影越模糊
float penumbra = (currentDepth - avgBlockerDepth) / max(avgBlockerDepth, 0.0001);
// 动态 PCF 半径 = 半影宽度 × 光源尺寸，clamp 防止极端值
float pcfRadius = clamp(penumbra * lightSize, 0.5, 16.0);
```

**`lightSize`** = 光源的"虚拟尺寸"（单位：纹素）。越大 → 阴影越软。本引擎默认 6.0。

**直觉**：`currentDepth - avgBlockerDepth` 越大，说明遮挡物离当前片元越远，阴影应该越模糊 → PCF 半径越大。

### Step 3：用动态半径做 PCF

用 Step 2 算出的 `pcfRadius` 调用 `PCFVariable`：

```glsl
return PCFVariable(proj.xy, refDepth, pcfRadius, texelSize);
```

**结果**：靠近遮挡物的阴影很锐利；远离遮挡物的阴影自然变软。

---

## 13. PCSS 完整代码

```glsl
// 阴影主入口函数：根据模式选择 PCF 或 PCSS
// 输入：lightSpacePos = 片元在光源裁剪空间的坐标，N = 表面法线，L = 指向光源的方向
// 输出：[0,1]，0 = 完全阴影，1 = 完全照亮
float ComputeDirectionalShadow(vec4 lightSpacePos, vec3 N, vec3 L) {
    int mode = int(u_ShadowParams.w);   // 阴影模式：0=关, 1=PCF, 2=PCSS
    if (mode == 0) return 1.0;          // 阴影关闭，直接返回"全部照亮"

    // ---- 坐标变换：裁剪空间 → [0,1]³ ----
    vec3 proj = lightSpacePos.xyz / lightSpacePos.w;  // 透视除法（正交投影下 w=1）
    proj = proj * 0.5 + 0.5;                          // NDC [-1,1] → UV+深度 [0,1]

    // 超出 Shadow Map 范围 → 默认不遮挡
    if (proj.z > 1.0 || proj.z < 0.0)              return 1.0;  // 超出近远面
    if (any(lessThan(proj.xy, vec2(0.0))) ||
        any(greaterThan(proj.xy, vec2(1.0))))       return 1.0;  // 超出 UV 边界

    // ---- Bias 计算 ----
    float texel      = u_ShadowParams.z;            // = 1 / ShadowMapSize（一个纹素的 UV 宽度）
    float depthBias  = u_ShadowParams.x;            // 深度偏移量（消 acne）
    float normalBias = u_ShadowParams.y;            // 法线偏移量（进一步消 acne）
    float NdotL      = max(dot(N, L), 0.0);         // 光线和法线的余弦
    float bias       = max(depthBias * (1.0 - NdotL), depthBias * 0.1); // 光越斜 bias 越大
    bias            += normalBias * texel;           // 加上法线方向偏移
    float refDepth   = proj.z - bias;               // 最终用于比较的深度

    // ---- PCSS 模式（mode == 2）：动态半影 ----
    if (mode == 2) {
        float lightSize    = max(u_ShadowParams2.y, 0.0); // 虚拟光源尺寸（纹素）
        float searchRadius = 4.0;                          // blocker 搜索半径

        // Step 1: 搜索遮挡物平均深度
        float avgBlocker = FindBlockerDepth(proj.xy, refDepth, searchRadius, texel);
        if (avgBlocker < 0.0) return 1.0;           // 没有 blocker → 不在阴影中

        // Step 2: 根据遮挡距离估算半影宽度
        float penumbra  = (refDepth - avgBlocker) / max(avgBlocker, 0.0001);
        float pcfRadius = clamp(penumbra * lightSize, 0.5, 16.0); // 动态 PCF 半径

        // Step 3: 用动态半径做 PCF
        return PCFVariable(proj.xy, refDepth, pcfRadius, texel);
    }

    // ---- PCF 模式（mode == 1）：固定或可调半径 ----
    float pcfRadius = u_ShadowParams2.x;            // 从 UBO 读取 PCF 半径
    if (pcfRadius <= 1.001)                          // 半径 ≤ 1 → 用更快的 3×3
        return PCF3x3(proj.xy, refDepth, texel);
    return PCFVariable(proj.xy, refDepth, pcfRadius, texel); // 否则用可变核
}
```

---

# 第五部分：工程集成

---

## 14. 文件结构

```
Redflag/src/Redflag/Renderer/
    SceneRenderer.h / .cpp   ← Shadow Pass 调度 + ShadowSettings
    Renderer3D.h / .cpp       ← 深度 Pass 执行 + Shadow UBO 数据上传

RedflagForge/assets/shaders/
    ShadowDepth.glsl          ← Shadow Pass 的简单深度着色器
    Renderer3D_Mesh.glsl      ← 主 PBR 着色器（内含阴影采样函数）
```

---

## 15. 数据流

```
               CPU 端                                  GPU 端
    ┌────────────────────────┐              ┌─────────────────────────┐
    │ SceneRenderer          │              │ ShadowDepth.glsl        │
    │  TryBuildLightSpace()  │──matrix──►   │  顶点: LightSpace×Model │
    │  RunShadowPass()       │              │  片元: (空，只写深度)    │
    └──────────┬─────────────┘              └──────────┬──────────────┘
               │                                       │
      Shadow FBO 深度纹理 (2048²)                       │
               │                                       ▼
    ┌──────────┴─────────────┐              ┌─────────────────────────┐
    │ Renderer3D             │              │ Renderer3D_Mesh.glsl    │
    │  SetShadowData()       │──UBO + ──►   │  读 u_ShadowMap         │
    │  BindTexture(slot 11)  │  texture     │  ComputeDirectionalShadow│()
    │  EndScene()            │              │  → shadow * radiance    │
    └────────────────────────┘              └─────────────────────────┘
```

---

## 16. UBO 布局

所有阴影数据通过 Camera UBO（binding = 0）传递：

```cpp
// Camera UBO（binding = 0），所有 shader 共享
struct CameraData {
    glm::mat4 ViewProjection;      // offset   0  (64 bytes) — 相机 VP 矩阵
    glm::vec4 ViewPosition;        // offset  64  (16 bytes) — 相机位置，w=IBL标志
    glm::mat4 LightSpaceMatrix;    // offset  80  (64 bytes) ← 阴影用：光源空间矩阵
    glm::vec4 ShadowParams;        // offset 144  (16 bytes) ← 阴影用：bias/mode 等
    glm::vec4 ShadowParams2;       // offset 160  (16 bytes) ← 阴影用：PCF半径/光源尺寸
};
```

**UBO**（Uniform Buffer Object）= 一块 GPU 端的内存块，用来传递一组相关的参数。好处是一次上传可以让所有 shader 共享，不用每个 shader 单独设置 uniform。

### ShadowParams 的打包

| 通道 | 含义 | 默认值 |
|---|---|---|
| `x` | Depth Bias | 0.0015 |
| `y` | Normal Bias | 1.0 |
| `z` | `1.0 / ShadowMapSize`（一个纹素在 UV 空间的大小） | 1/2048 |
| `w` | 阴影模式（0=关, 1=PCF, 2=PCSS） | 1 |

### ShadowParams2 的打包

| 通道 | 含义 | 默认值 |
|---|---|---|
| `x` | PCF Radius（1.0 = 3×3，>1 = 5×5 可变核） | 1.5 |
| `y` | PCSS Light Size（虚拟光源尺寸，影响半影宽度） | 6.0 |
| `z, w` | 保留 | 0 |

---

## 17. 调用时序

```cpp
// SceneRenderer::RenderScene() / RenderRuntime() 中的阴影执行时序：

// 1. 构造光源空间矩阵（找到第一个 CastShadow=true 的方向光）
glm::mat4 lightSpaceMatrix;
bool hasShadow = TryBuildLightSpaceMatrix(scene, lightSpaceMatrix);

// 2. Shadow Pass（如果有需要投射阴影的方向光）
if (hasShadow) {
    RunShadowPass(lightSpaceMatrix);       // 从光源视角渲染深度

    // 3. 把阴影参数注入 UBO（让主 shader 能读到）
    glm::vec4 params(DepthBias, NormalBias, 1.0f/2048.0f, Mode);  // x,y,z,w
    glm::vec4 params2(PcfRadius, LightSize, 0, 0);                // x,y,z,w
    Renderer3D::SetShadowData(lightSpaceMatrix, params, params2); // 写入 UBO

    // 4. 绑定 Shadow Map 到纹理槽 11（shader 里 u_ShadowMap 绑定在这个 slot）
    glBindTextureUnit(11, s_ShadowFBO->GetDepthAttachmentRendererID());
}

// 5. 正常渲染（Main Pass）—— shader 里会读 slot 11 的深度纹理做阴影判断
Renderer3D::EndScene();
```

---

## 18. 主 Shader 中阴影的使用位置

在方向光照循环中，只有**第一盏方向光**投射阴影（因为只有一张 Shadow Map）：

```glsl
// 方向光循环：只有第一盏方向光有阴影（因为只有一张 Shadow Map）
for (int i = 0; i < u_NumDirectionalLights; i++) {
    vec3 L = normalize(-u_DirLights[i].Direction.xyz); // 光线方向（从物体指向光源）

    // 第一盏灯查 Shadow Map，其余灯默认全部照亮
    float shadow = (i == 0)
        ? ComputeDirectionalShadow(v_LightSpacePos, N, L)  // 查阴影
        : 1.0;                                             // 无阴影

    Lo += CalcDirectionalLight(u_DirLights[i], N, V, albedo, metallic, roughness, F0, shadow);
}
```

`CalcDirectionalLight` 的最后一步：

```glsl
// shadow 乘在最终光照结果上：
// shadow = 1.0 → 正常光照
// shadow = 0.0 → 完全遮挡（只剩环境光）
// shadow = 0.3 → 30% 光照（半影区）
return (kD * albedo / PI + specular) * radiance * NdotL * shadow;
```

---

# 第六部分：常见参数的含义和调节

---

## 19. 编辑器面板参数

本引擎的编辑器有一个 Shadow 调试面板：

| 参数 | 含义 | 调大 | 调小 |
|---|---|---|---|
| **Mode** | 0=关, 1=PCF, 2=PCSS | — | — |
| **Depth Bias** | 深度偏移，消除 acne | acne 消失，但阴影可能脱离物体 | acne 出现 |
| **Normal Bias** | 沿法线方向偏移 | 进一步消 acne，但窄缝处阴影丢失 | acne 出现 |
| **PCF Radius** | PCF 采样范围 | 阴影边缘更柔和 | 阴影边缘更锐利 |
| **PCSS Light Size** | 虚拟光源大小 | 半影更宽（PCSS 模式下） | 半影更窄 |
| **Ortho Half Size** | 阴影覆盖范围 | 更大范围有阴影，但精度下降 | 精度高，但范围小 |

---

## 20. 参数速查表

```
Shadow Map          2048 × 2048          DEPTH32F       slot 11
Light Space Matrix  正交投影 × lookAt    Camera UBO     offset 80
ShadowParams        (bias, normalBias, 1/size, mode)    offset 144
ShadowParams2       (pcfRadius, lightSize, -, -)         offset 160

Shadow Pass shader  ShadowDepth.glsl     顶点变换到光源空间，片元为空
Main shader         Renderer3D_Mesh.glsl ComputeDirectionalShadow() 返回 [0,1]

默认值：
  depthBias    = 0.0015
  normalBias   = 1.0
  pcfRadius    = 1.5
  lightSize    = 6.0
  orthoHalfSize= 25.0
```

---

# 第七部分：常见问题与陷阱

---

## 21. 问题速查

| # | 现象 | 原因 | 解决 |
|---|---|---|---|
| 1 | 被照亮的面上有黑色条纹 | Shadow Acne：深度精度不够 | 增大 Depth Bias |
| 2 | 阴影和物体之间有亮缝 | Peter Panning：Bias 太大 | 减小 Depth Bias / Normal Bias |
| 3 | 场景边缘没有阴影 | 正交视锥太小 | 增大 Ortho Half Size |
| 4 | 阴影很糊（锯齿严重） | 正交视锥太大，Shadow Map 像素被摊开 | 减小 Ortho Half Size 或加大 Shadow Map 分辨率 |
| 5 | 只有第一盏灯有阴影 | 当前只实现了一张 Shadow Map | 正常限制，后续可扩展为 CSM（级联阴影） |
| 6 | 半透明物体不投射阴影 | Shadow Pass 中跳过了半透明物体 | 正常限制 |
| 7 | 阴影边缘完全锐利 | Mode = 0 或 PCF 未启用 | 切到 Mode 1 或 2 |
| 8 | PCSS 模式下阴影全是软的 | Light Size 太大 | 减小 PCSS Light Size |
| 9 | 物体自身有奇怪的全黑面 | NdotL 和 bias 配合不当 | 调整 bias 的 NdotL 乘数 |
| 10 | ShadowMap 绑定后主 Pass 看不到 | 纹理槽没绑对（应该是 slot 11） | 检查 `glBindTextureUnit(11, ...)` |

---

# 第八部分：总结

---

## 22. 一句话回顾

1. **Shadow Mapping 的核心** = 从光源看一遍场景记录深度，正式渲染时拿深度做比较
2. **Shadow Acne** = 深度精度不够导致的条纹 → 用 Bias 修
3. **PCF** = 在 Shadow Map 上多采几个点取平均 → 柔化阴影边缘
4. **PCSS** = 先搜索遮挡物的平均深度 → 估算半影宽度 → 用动态 PCF 半径 → 越远越软

## 23. 和 IBL 的对比

| | Shadow Mapping | IBL |
|---|---|---|
| 解决的问题 | "这个像素是否被遮挡？" | "环境光贡献了多少光？" |
| 核心手段 | 从光源渲染一遍深度 | 把半球积分预计算成贴图 |
| 渲染通道 | 两遍（Shadow Pass + Main Pass） | 预计算一次 + 每帧查表 |
| 运行时开销 | 每帧多一遍几何 + 纹理采样 | 几乎零（三次查表） |
| 质量瓶颈 | Shadow Map 分辨率 / Bias 调节 | HDR 精度 / PrefilterMap 样本数 |

---

*End of Guide*
