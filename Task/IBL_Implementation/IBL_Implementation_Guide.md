# IBL（基于图像的光照）完整实现指南

> **阅读对象**：刚入门实时图形学，听过 PBR 但没写过 IBL 的程序员。
>
> **阅读方式**：按顺序从头读到尾。每一章都只依赖前面的章节，不会向后引用。
>
> **约定**：
> - 所有代码基于 OpenGL 4.5 + GLSL 450
> - 本项目叫 Redflag（一个类 Hazel 的引擎）
> - 所有数学符号在第一次出现时都会解释

---

# 第一部分：为什么需要 IBL

---

## 1. 问题的起点

实时 PBR 渲染中，直接光照（点光源、方向光）只是一部分。现实世界里一个物体接收的光大部分来自**周围环境的间接反射**——天空、墙壁、地面。

如果我们只算直接光，没有被光源照到的面就是纯黑的。这显然不真实。

**环境光**就是用来补偿这部分缺失的"来自四面八方的间接光"。

---

## 2. 传统做法与它的不足

最简单的做法：`ambient = vec3(0.03) * albedo`——一个常数。

问题：
- 金属球和塑料球的环境光一样，不合理
- 粗糙表面和光滑表面的环境反射一样，不合理
- 环境颜色和场景无关（永远是灰白色），不合理

**IBL 就是为了解决这三个问题**：用一张真实的环境照片（HDR 图）作为光源，让环境光的颜色、强度、方向都来自真实场景。

---

## 3. IBL 要算的东西

用严格的数学语言，IBL 要算的是**反射方程**中"光源 = 整个环境"的情况：

$$
L_o(\mathbf{V}) = \int_\Omega f_r(\mathbf{L}, \mathbf{V})\; L_i(\mathbf{L})\; (\mathbf{N} \cdot \mathbf{L})\; d\mathbf{L}
$$

先不要被吓到。逐项翻译：

| 符号 | 含义 |
|---|---|
| $L_o(\mathbf{V})$ | 朝相机方向射出多少光（= 我们要算的像素颜色） |
| $\int_\Omega$ | 对表面法线 N 上方的整个半球求和 |
| $f_r(\mathbf{L}, \mathbf{V})$ | BRDF：从 L 方向进来的光有多少朝 V 方向反射（后面会详细讲） |
| $L_i(\mathbf{L})$ | 从 L 方向进来的光有多亮（= 环境贴图在 L 方向的颜色） |
| $(\mathbf{N} \cdot \mathbf{L})$ | 余弦项：光越斜打过来，单位面积接收的能量越少 |
| $d\mathbf{L}$ | 对所有入射方向做无穷小的累加 |

**问题**：这个积分涉及整个半球上无穷多个方向。对每个像素、每一帧都算一遍？不可能。

**解决**：Split Sum——把这个不可能实时算的积分，拆成几张可以提前算好的贴图，运行时只做几次查表。

---

## 4. Split Sum：把一个大积分拆成两半

Epic Games 在 UE4 中提出的核心思想（Karis, 2013）：

$$
\int_\Omega f_r\, L_i\, (\mathbf{N}\!\cdot\!\mathbf{L})\, d\mathbf{L}
\;\approx\;
\underbrace{\int_\Omega L_i(\mathbf{L})\, d\mathbf{L}}_{\text{只和环境有关}}
\;\times\;
\underbrace{\int_\Omega f_r\, (\mathbf{N}\!\cdot\!\mathbf{L})\, d\mathbf{L}}_{\text{只和材质有关}}
$$

左边的"环境部分"可以进一步分成漫反射和镜面反射两路：

**漫反射路**：环境光在半球上做 cosine 加权平均 → **IrradianceMap**
**镜面反射路**：再拆一次
- 按反射方向和粗糙度预滤波环境 → **PrefilterMap**
- 按视角和粗糙度预积分 BRDF → **BRDF LUT**

所以最终我们需要预计算 **3 张贴图**：

| 贴图 | 存什么 | 格式 | 尺寸 |
|---|---|---|---|
| **IrradianceMap** | 每个法线方向的漫反射环境光总量 | Cubemap, RGB16F | 32 × 32 × 6 面 |
| **PrefilterMap** | 不同粗糙度下、每个反射方向的镜面环境光 | Cubemap, RGB16F | 128 × 128 × 6 面, 5 级 mip |
| **BRDF LUT** | 不同 (视角, 粗糙度) 下 BRDF 的积分值 | 2D, RG16F | 512 × 512 |

运行时，主 shader 只需要做：

```glsl
// 漫反射 IBL
vec3 irradiance = texture(IrradianceMap, N).rgb;
vec3 diffuseIBL = irradiance * albedo;

// 镜面 IBL（Split Sum 的两半相乘）
vec3 prefiltered = textureLod(PrefilterMap, R, roughness * 4.0).rgb;
vec2 brdf        = texture(BRDFLUT, vec2(NdotV, roughness)).rg;
vec3 specularIBL = prefiltered * (F * brdf.x + brdf.y);

// 合成
vec3 ambient = kD * diffuseIBL + specularIBL;
```

**三次查表代替一个半球积分**——这就是 IBL 的全部核心。

---

## 5. 预计算的整体流水线

在程序启动时（`Init`），按顺序执行 5 步：

```
HDR 文件 (.hdr)
    │
    ▼  Step 1: 加载为 2D 纹理
 2D HDR 纹理 (equirectangular)
    │
    ▼  Step 2: 投影到立方体 6 面
 EnvCubemap (512³, RGB16F)           ← 中间产物，也用来画天空盒
    │
    ├───────────────────┐
    ▼  Step 3           ▼  Step 4
 IrradianceMap        PrefilterMap        Step 5（独立）:
 (32³)                (128³, 5 mip)       BRDF LUT (512², 2D)
```

每一步的输入/输出/关键参数在后面会逐一展开。

---

# 第二部分：前置知识

> 如果你已经熟悉 PBR 的 Cook-Torrance 模型，可以跳到第三部分。
> 如果你是第一次接触这些概念，**务必读完这一部分**，否则后面的推导会处处卡壳。

---

## 6. 方向与角度

### 6.1 五个关键向量

在着色一个像素时，我们关心五个方向（都是**单位向量**，从着色点出发）：

| 向量 | 名称 | 含义 | 怎么得到 |
|---|---|---|---|
| **N** | 法线 | 表面朝外的方向 | 顶点属性 / 法线贴图 |
| **V** | 视线 | 从着色点指向相机 | `normalize(cameraPos - fragPos)` |
| **L** | 光线 | 从着色点指向光源 | 点光源位置 / 采样方向 |
| **H** | 半程向量 | V 和 L 的角平分线 | `normalize(V + L)` |
| **R** | 反射向量 | V 关于 N 的镜面反射 | `reflect(-V, N)` |

### 6.2 为什么需要半程向量 H

微表面模型假设：表面由无数个微小镜面组成，每个微小镜面朝不同方向。只有微小镜面的法线恰好指向 H 时，它才能把 L 方向的光反射到 V 方向。所以 H 在 Cook-Torrance 公式里是核心变量。

### 6.3 常见的点积缩写

| 缩写 | 数学 | 意义 |
|---|---|---|
| `NdotL` | `max(dot(N,L), 0)` | 光线和表面有多正对；0 = 擦边 / 背面 |
| `NdotV` | `max(dot(N,V), 0)` | 视线和表面有多正对 |
| `NdotH` | `max(dot(N,H), 0)` | 微表面法线和 H 有多对齐 |
| `HdotV` | `max(dot(H,V), 0)` | 用于 Fresnel 计算 |

`max(..., 0)` 是因为负值意味着方向在表面背面，物理上没有贡献。

---

## 7. BRDF：Cook-Torrance 模型

### 7.1 BRDF 的定义

BRDF（双向反射分布函数）回答一个问题：

> "从 L 方向来一束光，有多少比例会朝 V 方向反射？"

它是上面反射方程里的 $f_r$。

### 7.2 漫反射 + 镜面反射

PBR 把反射分成两部分：

- **漫反射**（diffuse）：光进入材质内部，被散射后从各个方向均匀射出。BRDF 是一个常数 `albedo / π`。
- **镜面反射**（specular）：光直接在材质表面弹回。方向集中在反射方向附近。

$$
f_r = k_D \cdot \frac{\text{albedo}}{\pi} + k_S \cdot \frac{D \cdot F \cdot G}{4 \cdot (N\!\cdot\!V)(N\!\cdot\!L)}
$$

其中 $k_D + k_S = 1$（能量守恒），$k_S$ 由 Fresnel 决定。

### 7.3 D — 法线分布函数（NDF）

回答："微表面里有多大比例的法线朝着 H 方向？"

我们用 **GGX**（也叫 Trowbridge-Reitz）：

$$
D(H) = \frac{\alpha^2}{\pi \big((N\!\cdot\!H)^2(\alpha^2-1)+1\big)^2}
$$

- $\alpha = \text{roughness}^2$（为什么平方？见 7.6）
- roughness = 0 时 D 是一个极窄的尖峰（完美镜面）
- roughness = 1 时 D 几乎是一个半球均匀分布

```glsl
// GGX / Trowbridge-Reitz 法线分布函数
// 输入：NdotH = dot(N, H)，roughness = 美术可调的粗糙度 [0,1]
// 输出：D 值，代表有多少微表面法线朝向 H
float DistributionGGX(float NdotH, float roughness) {
    float a  = roughness * roughness;           // α = roughness²（Disney 映射，让美术感知线性）
    float a2 = a * a;                            // α² = roughness⁴，代入公式分子分母都要用
    float d  = NdotH * NdotH * (a2 - 1.0) + 1.0; // 公式分母根号下的部分：(NdotH²)(α²-1)+1
    return a2 / (PI * d * d);                    // 完整公式：α² / (π · d²)
}
```

### 7.4 F — Fresnel（菲涅尔）

回答："入射角不同时，反射了多少、折射（透过）了多少？"

日常经验：从正上方看水面能看到水底，从侧面看水面像一面镜子。这就是 Fresnel 效应。

用 Schlick 近似：

$$
F = F_0 + (1-F_0)(1 - H\!\cdot\!V)^5
$$

- $F_0$ = 正对时的反射率（非金属 ≈ 0.04，金属 = albedo）
- $H\!\cdot\!V$ 越小（视线越斜），$F$ 越接近 1（全反射）

```glsl
// Schlick 近似的菲涅尔方程
// 输入：HdotV = dot(H, V)，F0 = 正对入射时的反射率（非金属≈0.04，金属=albedo）
// 输出：当前视角下的反射率 [F0, 1]（视线越斜反射越强）
vec3 FresnelSchlick(float HdotV, vec3 F0) {
    // clamp 防止 pow 对负数报错，(1-HdotV)^5 在 HdotV→0（掠射角）时趋近 1
    return F0 + (1.0 - F0) * pow(clamp(1.0 - HdotV, 0.0, 1.0), 5.0);
}
```

### 7.5 G — 几何遮蔽

回答："微表面的凹凸不平导致多少光被自己挡住了？"

粗糙表面有很多"山丘"和"山谷"。一些反射的光在离开前就被相邻的突起挡住了（shadowing），或者入射光根本到不了某些凹处（masking）。

用 Smith-GGX：

$$
G_1(\mathbf{X}) = \frac{N\!\cdot\!X}{(N\!\cdot\!X)(1-k)+k}, \qquad G = G_1(V) \cdot G_1(L)
$$

**注意 k 的取值**：
- 直接光：$k = (\text{roughness}+1)^2 / 8$
- IBL：$k = \alpha^2 / 2 = \text{roughness}^4 / 2$（后面 BRDF LUT 会用到）

```glsl
// Schlick-GGX 几何遮蔽项（单方向）
// 输入：NdotX = dot(N, V) 或 dot(N, L)，k = 粗糙度相关的遮蔽系数
// 输出：[0,1]，1 = 完全不被遮蔽
// 注意 k 的取值：直接光 k=(roughness+1)²/8；IBL k=α²/2=roughness⁴/2
float GeometrySchlickGGX(float NdotX, float k) {
    return NdotX / (NdotX * (1.0 - k) + k);  // NdotX 越小（掠射角）→ 遮蔽越严重
}

// Smith 方法：分别计算观察方向和入射方向的遮蔽，再相乘
float GeometrySmith(float NdotV, float NdotL, float k) {
    return GeometrySchlickGGX(NdotV, k)   // 视线方向的遮蔽（masking）
         * GeometrySchlickGGX(NdotL, k);  // 光线方向的遮蔽（shadowing）
}
```

### 7.6 α = roughness² 是怎么回事

美术调节 roughness 时，希望 0.1→0.2 和 0.8→0.9 的视觉变化差不多大。但 GGX 的数学对小值非常敏感。把 roughness 先平方一下再代入公式，视觉感知就近似线性了。这是 Disney 提出的工业约定。

### 7.7 材质参数速查

| 参数 | 取值范围 | 含义 |
|---|---|---|
| `albedo` | RGB [0,1] | 漫反射基色 |
| `metallic` | [0,1] | 金属度；1 = 无漫反射 |
| `roughness` | [0,1] | 粗糙度；0 = 完美镜面 |
| `ao` | [0,1] | 环境光遮蔽 |
| `F0` | RGB | 正对反射率；非金属 = 0.04，金属 = albedo |

---

## 8. 积分的数值计算方法

### 8.1 为什么需要数值积分

反射方程里的积分没有解析解（闭合公式），必须用**数值方法近似计算**。

### 8.2 黎曼求和（最直觉的方法）

把积分域切成均匀的小格子，每个格子里取一个值然后求和：

$$
\int_a^b f(x)\,dx \approx \sum_{i=1}^{N} f(x_i) \cdot \Delta x
$$

Irradiance 卷积用的就是这个方法（φ 和 θ 各自均匀步进）。

### 8.3 蒙特卡洛积分（更高效的方法）

当积分域是高维的、被积函数在大部分区域接近 0 时（比如镜面反射），均匀采样效率很低。

蒙特卡洛的做法：**不均匀采样，让样本集中在函数值大的地方，然后用概率密度修正权重**。

$$
\int f(x)\,dx \;\approx\; \frac{1}{N}\sum_{i=1}^{N} \frac{f(x_i)}{p(x_i)}
$$

- $p(x_i)$ 是你选的采样分布的**概率密度函数（PDF）**
- 除以 $p(x_i)$ 保证结果无偏：采样密集的地方每个样本的权重被减小，稀疏的地方被放大
- 当 $p$ 的形状和 $f$ 越接近，结果的方差越小 → 这就是**重要性采样**

### 8.4 低差异序列：比随机更好的"随机"

真随机数在低样本数时会出现聚簇（有些方向抽了好几次，有些方向一次没抽）。

**低差异序列**是确定性生成的点列，在任何子区间都分布均匀。IBL 用 **Hammersley 序列**：

```glsl
// Van der Corput 序列：把无符号整数的二进制位左右翻转，变成 [0,1) 的小数
// 例如：1=0b0001 → 翻转后=0b1000 = 0.5；3=0b0011 → 0b1100 = 0.75
// 这比真随机数分布更均匀，避免聊簇
// 下面的位操作分 5 步完成二进制反转（从 16bit 交换到 1bit 交换）
float RadicalInverse_VdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);                               // 高低 16 位交换
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u); // 相邻 1bit 交换
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u); // 相邻 2bit 交换
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u); // 相邻 4bit 交换
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u); // 相邻 8bit 交换
    return float(bits) * 2.3283064365386963e-10; // = bits / 2^32，映射到 [0,1)
}

// Hammersley 序列：生成第 i 个点（共 N 个）
// 返回 [0,1]² 的 2D 坐标，x 分量等距分布，y 分量用 VdC 保证二维均匀
vec2 Hammersley(uint i, uint N) {
    return vec2(float(i) / float(N),   // 第一维：i/N 等距分布
                RadicalInverse_VdC(i)); // 第二维：二进制翻转，与第一维低相关
}
```

### 8.5 GGX 重要性采样

给定 Hammersley 的 2D 点 $(ξ_1, ξ_2)$，按 GGX 的 NDF 分布生成一个 **半程向量 H**：

$$
\phi = 2\pi\,\xi_1, \qquad \cos\theta = \sqrt\frac{1-\xi_2}{1+(\alpha^2-1)\,\xi_2}
$$

这个公式的来历：把 GGX 的 NDF 写成关于 θ 的 PDF，对它做累积分布函数（CDF），再求反函数——这就是"反演法"。结果就是上面的公式。

然后把球面坐标转成笛卡尔坐标，再用切线基旋转到世界空间：

```glsl
// GGX 重要性采样：根据 GGX NDF 的分布生成一个半程向量 H
// 输入：Xi = Hammersley 2D 点，N = 表面法线，roughness = 粗糙度
// 输出：世界空间的半程向量 H，按 D(H) 概率密度分布
vec3 SampleGGX(vec2 Xi, vec3 N, float roughness) {
    float a = roughness * roughness;               // α = roughness²（和 D 函数保持一致）

    // 球面坐标：由 GGX CDF 的反函数推导而来
    float phi      = 2.0 * PI * Xi.x;             // 方位角 φ ∈ [0, 2π)，均匀分布
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a*a - 1.0) * Xi.y)); // 极角 θ，按 GGX 分布
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta); // sin² + cos² = 1

    // 切线空间的 H（z 轴 = N 方向）
    vec3 H_tangent = vec3(cos(phi)*sinTheta,       // x 分量
                          sin(phi)*sinTheta,       // y 分量
                          cosTheta);               // z 分量（沿 N 方向）

    // 构建切线基（把 N 当 z 轴，构造一组正交基）
    vec3 up      = abs(N.z) < 0.999 ? vec3(0,0,1) : vec3(1,0,0); // 避免 N 和 up 平行
    vec3 tangent = normalize(cross(up, N));        // 切线 = up × N
    vec3 bitan   = cross(N, tangent);              // 副切线 = N × tangent

    // 旋转到世界空间：tangent * x + bitangent * y + N * z
    return normalize(tangent*H_tangent.x + bitan*H_tangent.y + N*H_tangent.z);
}
```

### 8.6 概率密度函数（PDF）与变量变换

GGX 采样得到的是 **H**，但积分变量是 **L**。从 H 到 L 的关系是镜面反射：`L = reflect(-V, H) = 2*(V·H)*H - V`。

变量变换时 PDF 要乘上一个**雅可比（Jacobian）**系数：

$$
p(\mathbf{L}) = \frac{D(H) \cdot (N\!\cdot\!H)}{4 \cdot (H\!\cdot\!V)}
$$

这里的 $4 \cdot (H\!\cdot\!V)$ 就是从 H 空间到 L 空间的雅可比。它不是凑出来的，而是 `L = 2(V·H)H - V` 这个非线性变换的度量换算。

---

## 9. 球面坐标与立体角

### 9.1 球面坐标

用 $(φ, θ)$ 描述球面上的方向：
- $φ \in [0, 2π)$：绕轴旋转的角度（经度）
- $θ \in [0, π]$：从北极到南极的角度（Co-latitude）；在上半球积分时 $θ \in [0, π/2)$

方向转笛卡尔坐标：$(x, y, z) = (\sin\theta\cos\phi,\; \sin\theta\sin\phi,\; \cos\theta)$

### 9.2 立体角（Solid Angle）

立体角是"方向所占的面积"。完整球面 = $4\pi$ sr（球面度）。

微分形式：$d\omega = \sin\theta\, d\theta\, d\phi$

**为什么有一个 sinθ**：这是球面坐标的雅可比行列式。直觉：赤道附近的一小块 $(dθ, dφ)$ 包含的方向比极点附近同样大小的 $(dθ, dφ)$ 多。

### 9.3 Cubemap

一个 Cubemap 有 6 个正方形面（±X, ±Y, ±Z）。用方向向量 $(x,y,z)$ 采样时，GPU 找到和单位立方体的交点，读取对应面上的像素。

**关键性质**：如果把一个单位立方体放在原点，它某个顶点的坐标本身就是一个方向向量。所以在 vertex shader 里，`v_LocalPos = a_Pos` 就直接得到了采样方向。

---

## 10. HDR 与 Equirectangular 投影

### 10.1 HDR 图

普通图片像素值限制在 [0, 255]。HDR（High Dynamic Range）图的像素值可以是任意正浮点数——太阳像素可能是 10000，室内阴影角落可能是 0.001。

IBL 需要 HDR 是因为：真实环境的亮度范围极大，如果截断到 [0,1]，高亮区域的信息全部丢失。

### 10.2 Equirectangular 投影

`.hdr` 文件通常用"等距柱状投影"（equirectangular）——就像世界地图的墨卡托投影。

$$
u = \frac{\text{atan2}(z, x)}{2\pi} + 0.5, \qquad v = \frac{\text{asin}(y)}{\pi} + 0.5
$$

**缺点**：极点附近严重拉伸。所以我们第一步要把它转成 Cubemap，每个 texel 对应的立体角更均匀。

### 10.3 HDR 陷阱：+Inf 像素

真实 HDR 文件里太阳像素可能是 `+Inf`。`glGenerateMipmap` 对 Inf 求平均会产生 `NaN`，然后 NaN 会通过后续的 Irradiance / Prefilter 采样扩散到**整张贴图** → 模型变黑或出现彩色色块。

**解决方法**：加载 HDR 后立刻钳制所有像素到 [0, 50]：

```cpp
// HDR 像素钳制：消除太阳等极亮像素中的 +Inf 和 NaN
// 不做这一步 → glGenerateMipmap 对 Inf 求平均 → NaN 扩散到整张贴图
for (int i = 0; i < width * height * channels; ++i) {
    float v = pixels[i];
    if (!std::isfinite(v)) v = 0.0f;    // NaN 或 Inf → 归零
    else if (v > 50.0f)    v = 50.0f;   // 截断极亮值，50 足够表现 HDR 范围
    else if (v < 0.0f)     v = 0.0f;    // 负值无物理意义
    pixels[i] = v;
}
```

---

# 第三部分：预计算流水线（5 步详解）

> 每一步都按 **目标 → 输入 → 输出 → 核心 Shader → 注意事项** 的格式展开。

---

## 11. Step 1 — 加载 HDR 到 2D 纹理

**目标**：把 `.hdr` 文件变成一张 GPU 可用的 2D 浮点纹理。

**代码要点**：
```cpp
stbi_set_flip_vertically_on_load(true);                  // stb 默认上下颠倒，翻转后符合 OpenGL UV 布局
float* pixels = stbi_loadf(path, &width, &height, &channels, 0); // 加载为浮点像素，每个通道 float
// !! 㣁制 Inf / NaN（见 10.3）!! 必须在上传 GPU 前处理

glGenTextures(1, &hdrTexture);                           // 生成纹理对象
glBindTexture(GL_TEXTURE_2D, hdrTexture);                // 绑定到 2D 纹理槽
glTexImage2D(GL_TEXTURE_2D, 0,
    GL_RGB16F,                                           // 内部格式：16位浮点（足够存储 HDR 范围）
    width, height, 0,
    GL_RGB, GL_FLOAT, pixels);                           // 源数据格式：RGB float
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); // 双线性过滤（不用 mip）
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); // 边缘钳住，防止采样越界
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
stbi_image_free(pixels);                                 // 释放 CPU 内存，像素已在 GPU 上
```

---

## 12. Step 2 — Equirectangular → EnvCubemap

**目标**：把 2D HDR 纹理投影到一个 512³ 的 Cubemap 的 6 个面上。

**输入**：Step 1 的 2D HDR 纹理
**输出**：`EnvCubemap`（512 × 512 × 6 面，RGB16F）

**原理**：对 Cubemap 每个像素，其顶点坐标就是一个方向 → 换算成 equirect 的 UV → 采样 HDR 纹理。

**Vertex Shader**（后续几步通用）：
```glsl
#version 450 core
layout(location=0) in vec3 a_Pos;       // 单位立方体的顶点（-1 到 +1）
uniform mat4 u_View;                     // 每个面的观察矩阵（分别朝 ±X/±Y/±Z）
uniform mat4 u_Projection;               // 90° FOV 的透视投影（刚好覆盖一个面）
out vec3 v_LocalPos;
void main() {
    v_LocalPos = a_Pos;                  // 顶点坐标 = Cubemap 采样方向
    gl_Position = u_Projection * u_View * vec4(a_Pos, 1.0);
}
```

**Fragment Shader**：
```glsl
#version 450 core
in vec3 v_LocalPos;                       // 从 VS 插值过来的方向向量
out vec4 FragColor;
uniform sampler2D u_EquirectMap;           // 等距柱状投影的 HDR 2D 纹理

// 1/(2π) 和 1/π，用于把 atan/asin 的弧度范围映射到 [0,1]
const vec2 invAtan = vec2(0.1591, 0.3183);

// 把 3D 方向向量转换成 equirectangular 的 UV 坐标
vec2 SampleSphericalMap(vec3 v) {
    // atan(z,x) ∈ [-π, π]  → 经度； asin(y) ∈ [-π/2, π/2] → 纬度
    vec2 uv = vec2(atan(v.z, v.x), asin(v.y));
    uv = uv * invAtan + 0.5;             // 线性变换到 [0,1]²
    return uv;
}

void main() {
    vec3 dir = normalize(v_LocalPos);     // 归一化为单位方向
    // 方向 → UV → 采样 HDR 纹理，写入 Cubemap 当前面
    FragColor = vec4(texture(u_EquirectMap, SampleSphericalMap(dir)).rgb, 1.0);
}
```

**渲染过程**：一个 FBO + 一个单位立方体 VAO，循环 6 次，每次换一个 view 矩阵（分别朝 ±X, ±Y, ±Z 看），把结果写入 Cubemap 对应的面：

```cpp
// 循环渲染 Cubemap 的6个面（+X, -X, +Y, -Y, +Z, -Z）
for (int i = 0; i < 6; ++i) {
    // 每个面用不同的 view 矩阵（分别朝 6 个方向看）
    glUniformMatrix4fv(viewLoc, 1, GL_FALSE, &captureViews[i][0][0]);
    // 把 FBO 的颜色附件指向 Cubemap 的第 i 个面
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, // +i 对应 6 个面的枚举值
                           envCubemap, 0);                     // mip level 0
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);        // 清空上一个面的数据
    DrawCube();  // 画单位立方体，VS 中 a_Pos 就是采样方向，FS 锜射到 HDR 纹理
}
```

**注意**：
- 6 个 view 矩阵使用 `glm::lookAt`，**±Y 面的 up 向量必须是 ±Z**（OpenGL Cubemap 规范要求）
- 创建 Cubemap 时先设 `MIN_FILTER = LINEAR`（不是 `LINEAR_MIPMAP_LINEAR`！）
- 渲染完 6 面后再调 `glGenerateMipmap(GL_TEXTURE_CUBE_MAP)`，然后才切到 `LINEAR_MIPMAP_LINEAR`
- 如果先设了 mipmap filter 却没有 mipmap 数据，采样返回黑色

---

## 13. Step 3 — Irradiance 卷积

**目标**：对 EnvCubemap 做半球 cosine 加权平均，生成 IrradianceMap。

**输入**：EnvCubemap（512³）
**输出**：IrradianceMap（32 × 32 × 6 面，RGB16F）

### 13.1 从公式到代码

我们要算的是：

$$
E(\mathbf{N}) = \int_\Omega L_i(\omega)\, (\mathbf{N}\!\cdot\!\omega)\, d\omega
$$

**逐部分翻译**：

**1) 积分域**：以 N 为轴的上半球。用球面坐标 $(φ, θ)$：
- $φ \in [0, 2\pi)$：绕 N 旋转一圈
- $θ \in [0, \pi/2)$：从 N（θ=0）到赤道（θ=π/2）

**2) 把 $d\omega$ 展开**：$d\omega = \sin\theta\, d\theta\, d\phi$

代入后：

$$
E(\mathbf{N}) = \int_0^{2\pi}\int_0^{\pi/2} L_i(\omega)\, \cos\theta\, \sin\theta\, d\theta\, d\phi
$$

注意 **两个三角函数的来源不同**：
- $\cos\theta$ 来自 $N \cdot \omega$ —— 光越斜打过来，贡献越小
- $\sin\theta$ 来自 $d\omega$ —— 赤道附近单位 (dθ, dφ) 对应的立体角更大

**3) 离散化**（均匀步长 δ = 0.025）：

$$
E(\mathbf{N}) \approx \frac{\pi}{\text{count}} \sum_{\phi}\sum_{\theta} L_i(\omega)\, \cos\theta\, \sin\theta
$$

**π / count 是怎么来的**：半球上 $\int_0^{2\pi}\int_0^{\pi/2} \cos\theta\,\sin\theta\,d\theta\,d\phi = \pi$。均匀采样时，每个样本的权重应该是"总权重 / 样本数"。

> 附注：最终 shader 输出的 IrradianceMap 已经包含了 π 因子。主 shader 里算漫反射时除以 π（因为 Lambert BRDF = albedo/π），两个 π 正好抵消。所以运行时直接 `irradiance * albedo` 就行了。

**4) 坐标变换**：球面坐标 → 切线空间方向 → 世界空间方向。

```glsl
// 切线空间方向：由球面坐标 (θ, φ) 转换而来
// x = sinθ·cosφ, y = sinθ·sinφ, z = cosθ
vec3 tangentSample = vec3(sin(theta)*cos(phi),
                          sin(theta)*sin(phi),
                          cos(theta));

// 需要一组正交基 (right, up, N) 把切线空间转到世界空间
// 先选一个不和 N 平行的 up→，用叉积构造正交基
vec3 up    = vec3(0, 1, 0);
vec3 right = normalize(cross(up, N));  // right = up × N
up         = normalize(cross(N, right)); // 重新算 up 保证正交

// 世界空间方向 = 切线空间分量分别乘对应基向量
vec3 sampleDir = tangentSample.x * right + tangentSample.y * up + tangentSample.z * N;
```

### 13.2 完整 Fragment Shader

```glsl
#version 450 core
in vec3 v_LocalPos;
out vec4 FragColor;
uniform samplerCube u_EnvMap;

const float PI = 3.14159265359;

void main() {
    vec3 N = normalize(v_LocalPos);       // Cubemap 面上这个像素对应的法线方向
    vec3 irradiance = vec3(0.0);          // 累加器：最终辐照度

    // 构建切线基：以 N 为 z 轴的局部坐标系
    vec3 up    = vec3(0.0, 1.0, 0.0);     // 临时 up（可能和 N 接近平行，但这里猜测工作良好）
    vec3 right = normalize(cross(up, N)); // right = up × N
    up         = normalize(cross(N, right)); // 重新算 up 保证正交

    float delta = 0.025;                   // 步长（越小越精确，越慢）
    float count = 0.0;                     // 采样计数，用于归一化

    // 对半球做双重循环：方位角 φ × 极角 θ
    for (float phi = 0.0; phi < 2.0*PI; phi += delta) {         // φ ∈ [0, 2π)
        for (float theta = 0.0; theta < 0.5*PI; theta += delta) { // θ ∈ [0, π/2)
            // 球面坐标 → 切线空间方向
            vec3 ts = vec3(sin(theta)*cos(phi),   // x
                           sin(theta)*sin(phi),   // y
                           cos(theta));           // z（沿 N 方向）
            // 切线空间 → 世界空间
            vec3 sampleDir = ts.x * right + ts.y * up + ts.z * N;

            irradiance += texture(u_EnvMap, sampleDir).rgb
                        * cos(theta)    // N·ω 的余弦项：光越斜贡献越小
                        * sin(theta);   // dω 展开后的 sinθ：赤道附近立体角更大
            count += 1.0;
        }
    }
    irradiance = PI * irradiance / count;  // 归一化：半球 cos-weighted 总量 = π
    FragColor = vec4(irradiance, 1.0);
}
```

**渲染方式**：和 Step 2 一样，FBO + 6 个 view 矩阵循环，viewport 设成 32×32。

---

## 14. Step 4 — Prefilter（预滤波环境贴图）

**目标**：在不同粗糙度下，对环境贴图做方向性模糊，存入 Cubemap 的 mip 链。

**输入**：EnvCubemap（512³）
**输出**：PrefilterMap（128 × 128 × 6 面，5 级 mip，RGB16F）

### 14.1 从公式到代码

Split Sum 的左半：

$$
\text{Prefilter}(\mathbf{R}, \alpha) = \frac{\sum_i L_i(\mathbf{L}_i)\,(N\!\cdot\!L_i)}{\sum_i (N\!\cdot\!L_i)}
$$

这个求和已经是蒙特卡洛化简后的最终结果。下面详细推它是怎么来的。

**（1）原始积分**：

$$
\int_\Omega L_i(\omega)\, D(\omega, \mathbf{R}, \alpha)\, (\mathbf{N}\!\cdot\!\omega)\, d\omega
$$

**（2）关键简化 — 令 V = R = N**：

理论上 Prefilter 的结果依赖 (R, V, roughness) 三个变量。但一张 Cubemap 只能用方向索引，装不下 V。所以假设观察方向 V = 反射方向 R = 法线 N。

- 代价：掠射角（视线几乎和表面平行）时镜面反射不准
- 收益：积分结果只依赖 (方向, roughness) → 可以存进 Cubemap 方向轴 + mip 层

**（3）重要性采样**：

均匀采样半球效率极低（镜面反射只在 R 附近很窄的锥内有贡献）。所以按 GGX 的 NDF 来抽 H，再反推 L：

```glsl
vec3 H = SampleGGX(Xi, N, roughness);          // 按 D(H) 的分布抽一个 H
vec3 L = normalize(2.0 * dot(V, H) * H - V);   // 从 H 反推入射方向 L
```

**（4）蒙特卡洛约分**：

把 $f/p$ 展开后，D 和 4、NdL 等项都会抵消，最终每个样本的贡献简化为 $L_i \cdot NdL$，归一化用 $\sum NdL$：

$$
\text{Prefilter} \approx \frac{\sum L_i(\mathbf{L}_i) \cdot (N\!\cdot\!L_i)}{\sum (N\!\cdot\!L_i)}
$$

这就是代码里 `color / totalWeight` 的来历。

**（5）roughness → mip 映射**：

约定 5 级 mip，线性映射：
- mip 0 → roughness = 0.00
- mip 1 → roughness = 0.25
- mip 2 → roughness = 0.50
- mip 3 → roughness = 0.75
- mip 4 → roughness = 1.00

运行时：`textureLod(PrefilterMap, R, roughness * 4.0)`

### 14.2 Filtered Importance Sampling（FIS）

低样本数 + HDR 太阳像素会产生 firefly（亮斑噪点）。解决方法：**每个样本根据它的 PDF 自适应选择采样的 mip 层级**。

PDF 小的样本（即"这个方向本来不太可能被抽到"）→ 它覆盖的立体角大 → 应该去采更模糊的 mip 层。

```glsl
// 单个 texel 覆盖的立体角（Cubemap 总共 4π sr / 6面 / size² 个 texel）
float saTexel  = 4.0 * PI / (6.0 * envMapSize * envMapSize);

// 该样本覆盖的等效立体角
float D        = DistributionGGX(NdotH, roughness);
float pdf      = (D * NdotH / (4.0 * HdotV)) + 0.0001;
float saSample = 1.0 / (float(SAMPLES) * pdf + 0.0001);

// 两者的比值取 log → mip 层级
float mipLevel = roughness == 0.0 ? 0.0 : 0.5 * log2(saSample / saTexel);
```

`0.5 * log2` 中 0.5 的来历：mip 每升一级面积变为 1/4（长宽各减半），取 log 底 4 = 0.5 × log 底 2。

### 14.3 完整 Fragment Shader

```glsl
#version 450 core
in  vec3 v_LocalPos;                      // 从 VS 插值的 Cubemap 采样方向
out vec4 FragColor;
uniform samplerCube u_EnvMap;              // Step 2 生成的环境 Cubemap (512³)
uniform float u_Roughness;                 // 当前 mip 层级对应的粗糙度
uniform float u_EnvMapSize;                // EnvCubemap 的分辨率（512）

const float PI      = 3.14159265359;
const uint  SAMPLES = 1024u;               // 采样数，越大越平滑但越慢

// --- Hammersley / SampleGGX / DistributionGGX：见第 8 章 ---

void main() {
    vec3 N = normalize(v_LocalPos);        // 当前像素对应的方向
    vec3 R = N;                            // Split Sum 简化：反射方向 = 法线
    vec3 V = R;                            // 视线 = 反射方向 = 法线

    vec3  color       = vec3(0.0);         // 加权颜色累加器
    float totalWeight = 0.0;               // 权重累加器（用于归一化）
    // 单个 texel 覆盖的立体角：总 4π sr / (6 面 × size² 个 texel)
    float saTexel     = 4.0 * PI / (6.0 * u_EnvMapSize * u_EnvMapSize);

    for (uint i = 0u; i < SAMPLES; ++i) {
        vec2 Xi = Hammersley(i, SAMPLES);  // 第 i 个低差异采样点
        vec3 H  = SampleGGX(Xi, N, u_Roughness);  // 按 GGX 分布抽取半程向量
        vec3 L  = normalize(2.0 * dot(V, H) * H - V); // 由 H 反推入射方向 L

        float NdotL = max(dot(N, L), 0.0); // 只累加上半球的贡献
        if (NdotL > 0.0) {
            // ---- Filtered Importance Sampling (FIS) ----
            float NdotH = max(dot(N, H), 0.0);
            float HdotV = max(dot(H, V), 0.0);
            float D     = DistributionGGX(NdotH, u_Roughness); // 当前样本的 NDF 值
            // 采样 PDF：D(H)·NdotH / (4·HdotV)，加小量防除零
            float pdf   = (D * NdotH / (4.0 * HdotV)) + 0.0001;
            // 该样本等效覆盖的立体角 = 1 / (N·pdf)
            float saSample = 1.0 / (float(SAMPLES) * pdf + 0.0001);
            // mip 层级：样本立体角越大 → 采更模糊的 mip，消除 firefly
            float mipLevel = u_Roughness == 0.0
                           ? 0.0  // roughness=0 直接采 mip 0（完美镜面）
                           : 0.5 * log2(saSample / saTexel); // 0.5 因为 mip 面积比是 4:1

            // 加权累加：颜色 × NdotL（蓝卡约分后剩下的权重）
            color       += textureLod(u_EnvMap, L, mipLevel).rgb * NdotL;
            totalWeight += NdotL;
        }
    }
    FragColor = vec4(color / totalWeight, 1.0); // 归一化：∑(Li·NdotL) / ∑NdotL
}
```

**渲染方式**：5 层 mip，每层循环 6 面。每个 mip 的 viewport = 128 >> mipLevel。

---

## 15. Step 5 — BRDF LUT

**目标**：预积分 BRDF 的 Fresnel 部分，存为 2D 查找表。

**输入**：无（纯数学计算，不依赖环境贴图）
**输出**：BRDF LUT（512 × 512，RG16F，2D 纹理）

### 15.1 从公式到代码

Split Sum 的右半：

$$
\int_\Omega f_r(\omega_i, \omega_o)\, (\mathbf{N}\!\cdot\!\omega_i)\, d\omega
$$

代入 Cook-Torrance：$f_r = DFG / (4 \cdot NdotV \cdot NdotL)$。

**关键一步**：把 Schlick Fresnel 展开。

$$
F = F_0 + (1-F_0)(1-H\!\cdot\!V)^5
$$

令 $F_c = (1-H\!\cdot\!V)^5$，则 $F = F_0(1-F_c) + F_c$。

**观察**：F 关于 $F_0$ 是**线性**的。所以整个积分可以写成：

$$
F_0 \cdot \underbrace{\int (1-F_c)\frac{DG}{4 \cdot NdotV \cdot NdotL}(N\!\cdot\!L)\,d\omega}_{A}
\;+\; \underbrace{\int F_c\frac{DG}{4 \cdot NdotV \cdot NdotL}(N\!\cdot\!L)\,d\omega}_{B}
$$

**A 和 B 都不含 F₀**，只依赖 $(NdotV, roughness)$ → 刚好可以存进一张 2D 纹理的 R 通道和 G 通道。

**这就是"为什么 BRDF LUT 只有 RG 两个通道"的根本原因**：Schlick 公式关于 F₀ 是线性的，积分可以拆成两项。

使用 GGX 重要性采样，蒙特卡洛约分后每个样本的贡献用一个叫 `G_Vis` 的量表示：

$$
G_{\text{Vis}} = \frac{G \cdot (H\!\cdot\!V)}{(N\!\cdot\!H)(N\!\cdot\!V)}
$$

这是把蒙卡的 `f/p` 展开后 D 项和 4 以及 NdotL 互相抵消后剩下的。

**k 值注意**：这里的 G 必须用 IBL 版本 $k = \alpha^2/2 = \text{roughness}^4/2$，不能用直接光版本。

### 15.2 坐标系选择

为了降维，固定 $N = (0, 0, 1)$，把 $V$ 放在 xz 平面：

$$
V = (\sqrt{1 - NdotV^2},\; 0,\; NdotV)
$$

这样 NdotL = L.z，NdotH = H.z，所有计算都只和标量 NdotV 有关。

### 15.3 完整 Fragment Shader

```glsl
#version 450 core
in  vec2 v_UV;                             // 全屏四边形的 UV，x = NdotV，y = roughness
out vec4 FragColor;

const float PI      = 3.14159265359;
const uint  SAMPLES = 1024u;               // 采样数

// --- Hammersley / SampleGGX：见第 8 章 ---

// IBL 专用的几何遮蔽项（k 的计算方式不同于直接光）
// 直接光：k = (roughness+1)²/8
// IBL：   k = α²/2 = (roughness²)²/2 = roughness⁴/2
float GeometrySchlickGGX_IBL(float NdotX, float roughness) {
    float a = roughness;                   // 注意：这里的 roughness 就是 α = roughness²
    float k = (a * a) / 2.0;              // k = α²/2（IBL 专用 k 值）
    return NdotX / (NdotX * (1.0 - k) + k); // 和直接光的公式相同，只是 k 不同
}

// Smith 几何项：分别算 V 和 L 两个方向的遮蔽
float GSmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return GeometrySchlickGGX_IBL(NdotV, roughness)  // masking（视线被挡）
         * GeometrySchlickGGX_IBL(NdotL, roughness); // shadowing（光线被挡）
}

// 预积分 BRDF：输出 (A, B)，其中 Fresnel ≈ F0·A + B
vec2 IntegrateBRDF(float NdotV, float roughness) {
    // 固定 N = (0,0,1)，把 V 放在 xz 平面，降维到只依赖 NdotV
    vec3 V = vec3(sqrt(1.0 - NdotV*NdotV), // V.x = sin(arccos(NdotV))
                  0.0,                       // V.y = 0（在 xz 平面）
                  NdotV);                    // V.z = cos = NdotV
    vec3 N = vec3(0.0, 0.0, 1.0);           // N 固定指向 z 轴

    float A = 0.0, B = 0.0;                 // F0 的系数(A) 和 常数项(B)
    for (uint i = 0u; i < SAMPLES; ++i) {
        vec2 Xi = Hammersley(i, SAMPLES);    // 低差异采样点
        vec3 H  = SampleGGX(Xi, N, roughness); // 按 GGX 抽 H
        vec3 L  = normalize(2.0 * dot(V, H) * H - V); // 由 H 反推 L

        float NdotL = max(L.z, 0.0);        // 因为 N=(0,0,1)，NdotL = L.z
        float NdotH = max(H.z, 0.0);        // NdotH = H.z
        float VdotH = max(dot(V, H), 0.0);

        if (NdotL > 0.0) {
            float G     = GSmith(N, V, L, roughness);  // 几何遮蔽项
            // G_Vis = 蓝卡约分后的核心量：D 和 4、NdotL 互相抵消后剩下的
            float G_Vis = (G * VdotH) / (NdotH * NdotV);
            // Schlick Fresnel 的 (1-HV)^5 部分
            float Fc    = pow(1.0 - VdotH, 5.0);
            A += (1.0 - Fc) * G_Vis;         // F0 的系数：当 Fc=0 时全部贡献给 A
            B += Fc * G_Vis;                  // 常数项：当 Fc=1 时全部贡献给 B
        }
    }
    return vec2(A, B) / float(SAMPLES);      // 蓝卡平均
}

void main() {
    // v_UV.x = NdotV ∈ [0,1]，v_UV.y = roughness ∈ [0,1]
    FragColor = vec4(IntegrateBRDF(v_UV.x, v_UV.y), 0.0, 1.0); // 只用 RG 两个通道
}
```

**渲染方式**：全屏四边形，viewport 512×512，一次性渲染。

---

# 第四部分：运行时——主 Shader 中的 IBL 合成

---

## 16. 在主 PBR Shader 里使用预计算结果

### 16.1 纹理声明

```glsl
layout(binding = 7) uniform samplerCube u_IrradianceMap;  // 漫反射辐照度（32³）
layout(binding = 8) uniform samplerCube u_PrefilterMap;    // 预滤波环境（128³，5 mip）
layout(binding = 9) uniform sampler2D   u_BRDFLUT;         // BRDF 查找表（512²，RG16F）
```

### 16.2 IBL 版 Fresnel

标准 Schlick 在粗糙表面上会过强（磨砂金属边缘不应该全白）。IBL 用一个修正版：

```glsl
// IBL 专用的 Fresnel：把光滑表面 Fresnel 抑制为 max(1-roughness, F0)
// 标准 Schlick 在粗糙表面边缘会返回接近 1（全白），不合理
// 这个版本用 max(1-roughness, F0) 代替 1.0，让粗糙表面边缘不至于全白
vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0)
             * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}
```

区别：`max(1 - roughness, F0)` 代替原来的 `1`，让粗糙表面的 Fresnel 上限更低。

### 16.3 合成逻辑

```glsl
vec3 ambient;
if (u_ViewPosition.w > 0.5) {                         // IBL 就绪标志（CPU 端设置）
    // ---- Fresnel + 能量守恒 ----
    vec3 kS = FresnelSchlickRoughness(NdotV, F0, roughness); // 镜面反射比例
    vec3 kD = (1.0 - kS) * (1.0 - metallic);          // 漫反射比例 = (1-kS)·(1-金属度)

    // ---- 漫反射 IBL ----
    vec3 irradiance = texture(u_IrradianceMap, N).rgb; // 法线方向的环境漫反射光
    vec3 diffuseIBL = irradiance * albedo;             // 漫反射 = 辐照度 × 基色

    // ---- 镜面 IBL（Split Sum 两半相乘）----
    vec3 R = reflect(-V, N);                           // 反射方向
    const float MAX_LOD = 4.0;                         // mipCount - 1
    vec3 prefiltered = textureLod(u_PrefilterMap, R, roughness * MAX_LOD).rgb; // 按粗糙度采对应 mip
    vec2 brdf        = texture(u_BRDFLUT, vec2(NdotV, roughness)).rg; // 查 BRDF LUT
    vec3 specularIBL = prefiltered * (kS * brdf.x + brdf.y); // Split Sum: 预滤波 × (F0·A + B)

    ambient = (kD * diffuseIBL + specularIBL) * ao;    // AO 只影响环境光
} else {
    ambient = vec3(0.03) * albedo * ao;                // 回退到常数环境光
}
vec3 color = ambient + Lo;  // Lo = 直接光照（点光+方向光）
```

### 16.4 每一行的依据

| 代码 | 依据 |
|---|---|
| `u_ViewPosition.w > 0.5` | 复用 vec4 的 w 分量作 IBL 就绪标志，CPU 端由 `IBLPrecompute::IsReady()` 写 1.0 |
| `kD = (1-kS) * (1-metallic)` | 两层限制：①能量守恒 `kD+kS≤1`；②金属没有漫反射 |
| `roughness * MAX_LOD` | 与 Step 4 的 roughness→mip 映射一致 |
| `kS * brdf.x + brdf.y` | BRDF LUT 的 R 通道是 F₀ 的系数（A），G 通道是常数项（B）；完整 Fresnel = F₀·A + B |
| `* ao` | AO 只影响环境光，不影响直接光 |

---

## 17. 天空盒渲染

用 EnvCubemap 画背景天空。

### 17.1 Vertex Shader

```glsl
#version 450 core
layout(location=0) in vec3 a_Pos;           // 单位立方体顶点
uniform mat4 u_View;        // 已去掉平移：mat4(mat3(originalView))，只保留旋转
uniform mat4 u_Projection;  // 相机透视投影矩阵
out vec3 v_LocalPos;
void main() {
    v_LocalPos = a_Pos;                     // 顶点坐标 = Cubemap 采样方向
    vec4 clip = u_Projection * u_View * vec4(a_Pos, 1.0);
    gl_Position = clip.xyww;  // 令 z = w → NDC 深度 = z/w = 1.0（永远在最远处）
}
```

**`clip.xyww` 技巧**：让天空盒的深度恒等于 1.0，任何其他物体都能画在它前面。配套需要把 `glDepthFunc` 改为 `GL_LEQUAL`（默认 `GL_LESS` 会把深度=1 的片元剔掉）。

**`mat4(mat3(view))`**：去掉 view 矩阵的平移列，让天空盒不跟随相机移动（只跟随旋转）。

### 17.2 Fragment Shader

```glsl
#version 450 core
in  vec3 v_LocalPos;                        // 插值后的 Cubemap 采样方向
out vec4 FragColor;
uniform samplerCube u_EnvMap;                // Step 2 生成的环境 Cubemap
void main() {
    vec3 color = texture(u_EnvMap, normalize(v_LocalPos)).rgb; // 按方向采样环境贴图
    color = color / (color + vec3(1.0));     // Reinhard tone mapping：HDR → [0,1]
    color = pow(color, vec3(1.0 / 2.2));     // Gamma 校正：线性 → sRGB
    FragColor = vec4(color, 1.0);
}
```

### 17.3 正交投影保护

当编辑器切换到正交视图时天空盒会错乱。解决：渲染天空盒前检测投影矩阵：

```cpp
// projection[3][3] > 0.5 说明是正交投影（透视投影该位置 = 0）
// 正交视图下 xyww 技巧会出错，跳过天空盒
if (projection[3][3] > 0.5f) return;
```

---

# 第五部分：代码组织与集成

---

## 18. 文件结构

```
Redflag/src/Redflag/Renderer/
    IBLPrecompute.h      ← 对外接口
    IBLPrecompute.cpp    ← 预计算流水线 + 内嵌 shader 源码

RedflagForge/assets/shaders/
    Renderer3D_Mesh.glsl ← 主 PBR shader，包含 IBL 采样
```

### 18.1 IBLPrecompute.h

```cpp
#pragma once
#include <cstdint>
#include <string>
#include <glm/glm.hpp>

namespace Redflag {
    // 预计算产出的 4 张纹理的 GPU ID
    struct IBLTextures {
        uint32_t EnvCubemap    = 0;  // 环境 Cubemap (512³)，也用于天空盒
        uint32_t IrradianceMap = 0;  // 辐照度图 (32³)，漫反射 IBL 用
        uint32_t PrefilterMap  = 0;  // 预滤波图 (128³, 5 mip)，镜面 IBL 用
        uint32_t BRDFLUT       = 0;  // BRDF 查找表 (512², RG16F)，Fresnel 积分用
    };

    class IBLPrecompute {
    public:
        static void Init(const std::string& hdrPath);  // 加载 HDR + 执行 5 步预计算
        static void Shutdown();                         // 释放所有 GPU 资源
        static void Bind();         // 绑定 3 张贴图到 slot 7/8/9（每帧调用）
        static void RenderSkybox(const glm::mat4& view, const glm::mat4& projection); // 画天空盒
        static bool IsReady();      // 预计算是否完成（用于 shader 的 IBL flag）
        static const IBLTextures& GetTextures();  // 获取纹理 ID（调试用）
    };
}
```

### 18.2 为什么不用引擎原有的 Shader / Framebuffer 抽象

1. 预计算需要渲染到 Cubemap 的 6 面 + 5 个 mip 层级，引擎的 `OpenGLFramebuffer` 只支持 2D 附件
2. 预计算 shader 只在 Init 时运行一次，不值得走 shaderc/SPIR-V 编译缓存
3. 内嵌 shader 源码避免外部文件依赖

原则：**一次性的工具代码用原生 API；长期热路径才上抽象层**。

---

## 19. 三个集成点

```cpp
// 1. 初始化：加载 HDR 并执行 5 步预计算（只在启动时调用一次）
void SceneRenderer::Init() {
    IBLPrecompute::Init("assets/textures/environment.hdr");
}

// 2. 每帧渲染：提交 mesh，结束场景，画天空盒
void SceneRenderer::RenderScene(Scene& scene, const EditorCamera& camera) {
    // ... 提交 mesh
    Renderer3D::EndScene();                        // EndScene 内部会调 IBLPrecompute::Bind()
    IBLPrecompute::RenderSkybox(camera.GetViewMatrix(), camera.GetProjection()); // 最后画天空
}

// 3. 关闭：释放所有 IBL 纹理和 shader
void SceneRenderer::Shutdown() {
    IBLPrecompute::Shutdown();
}
```

Renderer3D::BeginScene 里设置 IBL 就绪标志：
```cpp
s_Data.CameraBuffer.ViewPosition = glm::vec4(
    cameraPos,
    IBLPrecompute::IsReady() ? 1.0f : 0.0f   // w 分量 = IBL flag，shader 根据它决定走 IBL 还是回退
);
```

---

# 第六部分：Debug 方法与常见陷阱

---

## 20. 系统化的 Debug 流程

IBL 涉及 5 步预计算 + 运行时合成，出了问题如何定位？核心原则：**先确认每张贴图本身是对的，再看合成逻辑**。

### 20.1 贴图诊断模式

在 shader 里加一个临时宏，一次只显示一张贴图：

```glsl
// 贴图诊断模式：一次只看一张贴图，确认它本身是否正确
#define DEBUG_IBL 1   // 1 = 直接涂 IrradianceMap（应该是模糊的环境颜色）
                      // 2 = 直接涂 PrefilterMap（中等 mip 层的模糊反射）
                      // 3 = 直接涂 BRDF LUT（应该是红→白渐变的 2D 图）

#if DEBUG_IBL == 1
    FragColor = vec4(texture(u_IrradianceMap, N).rgb, 1.0);
    return;
#elif DEBUG_IBL == 2
    FragColor = vec4(textureLod(u_PrefilterMap, R, 2.0).rgb, 1.0);
    return;
#elif DEBUG_IBL == 3
    FragColor = vec4(texture(u_BRDFLUT, v_UV).rg, 0.0, 1.0);
    return;
#endif
```

**如果三张都正常但合成后不对**：问题在 16.3 的合成逻辑（kD/kS、binding slot 是否对应、IBL flag 是否传了）。

### 20.2 NaN 诊断

如果模型出现红/绿/蓝色块，高度怀疑 NaN。用 `isnan` 定位来源：

```glsl
// NaN 诊断：如果某张贴图采样结果包含 NaN，用颜色可视化定位源头
if (any(isnan(diffuseIBL)))  { FragColor = vec4(1,0,0,1); return; }  // 红 = Irradiance 含 NaN
if (any(isnan(specularIBL))) { FragColor = vec4(0,1,0,1); return; }  // 绿 = Prefilter 含 NaN
// 如果红绿都亮 → NaN 源头在更上游 → 大概率是 HDR 文件里的 +Inf（见 10.3）
```

如果红绿都亮 → 源头在更上游 → 大概率是 HDR 的 Inf（见 10.3）。

---

## 21. 常见陷阱清单

| # | 陷阱 | 症状 | 修复 |
|---|---|---|---|
| 1 | HDR 含 `+Inf` | 模型出现 NaN 色块 / 全黑 | 加载后钳制到 [0, 50] |
| 2 | Cubemap 先设了 `LINEAR_MIPMAP_LINEAR` 但还没 generateMipmap | 采样返回黑色 | 先用 `LINEAR`，mipmap 生成后再改 |
| 3 | Prefilter 高 roughness 有 firefly 亮斑 | 噪点 | 用 Filtered Importance Sampling（14.2） |
| 4 | 天空盒 xyww 技巧 + 正交投影 | 天空盒消失或错乱 | 检测 `projection[3][3] > 0.5` 时跳过 |
| 5 | Cubemap ±Y 面的 up 向量写错 | 顶面底面渲染颠倒 | up 必须是 ±Z（OpenGL Cubemap 规范） |
| 6 | 预计算完就删了 cubeVAO | 天空盒渲染时崩溃 | cubeVAO 保留到 Shutdown |
| 7 | View 矩阵没去平移 | 相机移动时穿出天空盒 | `mat4(mat3(view))` |
| 8 | IBL ready flag 没传 | Shader 走回退分支，永远是灰色环境光 | `u_ViewPosition.w` = 1.0 |
| 9 | BRDF LUT 的 G 项用了直接光的 k | 粗糙物体偏亮或偏暗 | IBL 必须用 k = α²/2 |
| 10 | Irradiance 忘乘 cos·sin | 辐照度数值偏差大 | 两个因子缺一不可（一个是兰伯特，一个是立体角微元） |

---

# 第七部分：总结

---

## 22. 一页速查表

```
=== 预计算产物 ===
EnvCubemap        512³        RGB16F    5 mip    → 天空盒 + Prefilter 源
IrradianceMap     32³         RGB16F    1 mip    → 漫反射 IBL
PrefilterMap      128³        RGB16F    5 mip    → 镜面 IBL（roughness = mip/4）
BRDFLUT           512² (2D)   RG16F     1 mip    → Fresnel 积分查找表

=== 纹理 binding ===
slot 7 = IrradianceMap
slot 8 = PrefilterMap
slot 9 = BRDFLUT

=== 运行时公式 ===
diffuseIBL  = texture(IrradianceMap, N).rgb * albedo
specularIBL = textureLod(PrefilterMap, R, roughness * 4) * (kS * brdf.x + brdf.y)
ambient     = (kD * diffuseIBL + specularIBL) * ao

=== 关键常数来源 ===
π / count                                半球 cos-weighted 立体角总量
4·(H·V)                                 H→L 的雅可比（镜面反射变换）
4π / (6·size²)                           Cubemap 单 texel 的立体角
0.5 * log2(saSample/saTexel)             FIS mip 层（面积 ∝ 4^mip）
k_IBL = α²/2                             IBL 几何项（区别于直接光 k=(r+1)²/8）
```

---

## 23. 回顾：从问题到解决

1. **问题**：环境光 `vec3(0.03)` 太假
2. **方案**：用真实 HDR 环境贴图做光源
3. **困难**：反射方程涉及半球积分，实时算不了
4. **核心思想**：Split Sum —— 把积分拆成"只和环境有关"+"只和材质有关"两半，分别预计算
5. **产物**：3 张贴图（Irradiance / Prefilter / BRDF LUT）
6. **运行时**：三次查表 + 简单乘加 → 得到物理正确的环境光

**理解了这条链路，IBL 就不再神秘了。**

---

*End of Guide*
