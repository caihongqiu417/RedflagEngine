// =====================================================================
// Renderer3D_Mesh.glsl — 前向 PBR 着色器 (Cook-Torrance BRDF)
//
// 渲染流程概览：
//   1. 顶点阶段：把模型空间顶点变换到世界空间，构造 TBN 矩阵
//   2. 片元阶段：采样 PBR 贴图 → 构造表面属性 → 逐光源累加
//                Cook-Torrance 镜面 + Lambert 漫反射 → 输出 HDR 颜色
//
// UBO 绑定约定：
//   binding 0 — Camera    : ViewProjection + 相机世界坐标
//   binding 1 — Transform : 逐物体模型矩阵
//   binding 2 — Material  : PBR 参数 + 贴图开关 + EntityID
//   binding 3 — Lights    : 方向光 × 1 + 点光源 × 16
//
// 纹理槽位约定：
//   slot 3 — BaseColorMap          (sRGB 漫反射)
//   slot 4 — NormalMap             (切线空间法线)
//   slot 5 — MetallicRoughnessMap  (G=粗糙度, B=金属度, glTF 规范)
//   slot 6 — AOMap                 (R 通道, 环境遮蔽)
//
// 输出：
//   location 0 — o_Color    : RGBA 颜色（含临时 tonemap + gamma）
//   location 1 — o_EntityID : 用于编辑器鼠标拾取的实体 ID
// =====================================================================

// =========================== 顶点着色器 =============================
#type vertex
#version 450 core

// ---- 顶点属性（与 MeshVertex / Mesh.cpp 布局一一对应）----
layout(location = 0) in vec3 a_Position;   // 模型空间位置
layout(location = 1) in vec3 a_Normal;     // 模型空间法线
layout(location = 2) in vec2 a_TexCoord;   // 第 0 组 UV
layout(location = 3) in vec3 a_Tangent;    // 模型空间切线（用于法线贴图）

// ---- Camera UBO (binding = 0) ----
// 由 Renderer3D::BeginScene() 每帧上传
layout(std140, binding = 0) uniform Camera
{
    mat4 u_ViewProjection;      // 观察 × 投影矩阵
    vec4 u_ViewPosition;        // xyz = 相机世界坐标（PBR 镜面反射需要视线方向）
};

// ---- Transform UBO (binding = 1) ----
// 由 Renderer3D 每次 DrawCall 上传
layout(std140, binding = 1) uniform Transform
{
    mat4 u_Transform;           // 模型矩阵（局部空间 → 世界空间）
};

// ---- 传递给片元着色器的插值变量 ----
layout(location = 0) out vec3  v_WorldPos;  // 世界空间位置（用于光照距离计算）
layout(location = 1) out vec2  v_TexCoord;  // UV 坐标
layout(location = 2) out vec3  v_Normal;    // 世界空间法线（无法线贴图时的后备）
layout(location = 3) out mat3  v_TBN;       // 切线空间 → 世界空间 的变换矩阵
                                            // mat3 占用 location 3, 4, 5

void main()
{
    // 1. 把顶点从模型空间变换到世界空间
    vec4 worldPos = u_Transform * vec4(a_Position, 1.0);
    v_WorldPos = worldPos.xyz;
    v_TexCoord = a_TexCoord;

    // 2. 构造法线矩阵：transpose(inverse(Model))
    //    用于正确变换法线（非均匀缩放时不能直接乘模型矩阵）
    mat3 normalMatrix = mat3(transpose(inverse(u_Transform)));

    // 3. 变换法线和切线到世界空间
    vec3 N = normalize(normalMatrix * a_Normal);
    vec3 T = normalize(normalMatrix * a_Tangent);

    // 4. Gram-Schmidt 正交化：确保 T ⊥ N（插值或变换可能破坏正交性）
    T = normalize(T - dot(T, N) * N);

    // 5. 副切线 B = N × T（构成右手坐标系）
    vec3 B = cross(N, T);

    v_Normal = N;
    v_TBN    = mat3(T, B, N);  // 列向量分别是 T, B, N

    // 6. 最终裁剪空间位置
    gl_Position = u_ViewProjection * worldPos;
}

// =========================== 片元着色器 =============================
#type fragment
#version 450 core

// ---- 输出 ----
layout(location = 0) out vec4 o_Color;     // RGBA 颜色
layout(location = 1) out int  o_EntityID;  // 编辑器鼠标拾取用的实体 ID（RED_INTEGER 附件）

// ---- 从顶点着色器插值而来 ----
layout(location = 0) in vec3  v_WorldPos;
layout(location = 1) in vec2  v_TexCoord;
layout(location = 2) in vec3  v_Normal;
layout(location = 3) in mat3  v_TBN;

// ---- Camera UBO (binding = 0) ----
layout(std140, binding = 0) uniform Camera
{
    mat4 u_ViewProjection;
    vec4 u_ViewPosition;
};

// ---- Material UBO (binding = 2, 48 bytes, std140) ----
// 由 Renderer3D::UploadMaterial() 在材质切换时上传
layout(std140, binding = 2) uniform Material
{
    vec4  u_BaseColor;               // 基础颜色因子（与 BaseColorMap 相乘）
    float u_Metallic;                // 金属度 [0,1]（0=非金属, 1=纯金属）
    float u_Roughness;               // 粗糙度 [0,1]（0=镜面, 1=完全粗糙）
    float u_AO;                      // 环境遮蔽强度 [0,1]（1=无遮蔽）
    int   u_UseBaseColorMap;         // 1 = 启用漫反射贴图
    int   u_UseNormalMap;            // 1 = 启用法线贴图
    int   u_UseMetallicRoughnessMap; // 1 = 启用金属度粗糙度贴图
    int   u_UseAOMap;                // 1 = 启用环境遮蔽贴图
    int   u_EntityID;                // 当前绘制实体的 entt ID（拾取用）
};

// ---- Lights UBO (binding = 3) ----
// 由 SceneRenderer::CollectAndUploadLights() 每帧上传
//
// 每个光源 64 字节 (std140 对齐)，布局：
//   Direction : xyz=归一化方向  w=类型(0=方向光, 1=点光)
//   Color     : xyz=RGB颜色    w=强度倍数
//   Position  : xyz=世界坐标   w=影响半径（仅点光有效）
//   CastShadow: 0 或 1（当前未使用，Phase 6 阴影用）
struct LightData
{
    vec4 Direction;
    vec4 Color;
    vec4 Position;
    int  CastShadow;
    int  _pad0;         // std140: 不用数组，避免每个元素被对齐到 vec4（16 字节）
    int  _pad1;
    int  _pad2;
};

layout(std140, binding = 3) uniform Lights
{
    int       u_NumDirectionalLights;  // 当前帧方向光数量（最多 1）
    int       u_NumPointLights;        // 当前帧点光源数量（最多 16）
    int       _lightPad0;              // 对齐填充（不用数组，避免 std140 按 vec4 对齐）
    int       _lightPad1;              // 对齐填充
    LightData u_DirLights[1];         // 方向光数组
    LightData u_PointLights[16];      // 点光源数组
};

// ---- 纹理采样器（槽位与 C++ 端 Bind(slot) 对应）----
layout(binding = 3) uniform sampler2D u_BaseColorMap;          // slot 3: 漫反射贴图
layout(binding = 4) uniform sampler2D u_NormalMap;             // slot 4: 切线空间法线贴图
layout(binding = 5) uniform sampler2D u_MetallicRoughnessMap;  // slot 5: G=粗糙度 B=金属度
layout(binding = 6) uniform sampler2D u_AOMap;                 // slot 6: R=环境遮蔽

// =====================================================================
// PBR 核心：Cook-Torrance 微面元 BRDF
//
// 完整公式：
//   f(l,v) = kD * (albedo/π) + (D · G · F) / (4 · (N·V) · (N·L))
//
// 其中：
//   D = 法线分布函数 (Normal Distribution Function)
//       → 控制微面元有多少比例朝向半程向量 H
//       → roughness 越大，高光越散
//   G = 几何遮蔽函数 (Geometry Function)
//       → 微面元之间互相遮挡和阴影的比例
//       → roughness 越大，遮挡越多
//   F = 菲涅尔函数 (Fresnel Function)
//       → 掠射角观察时反射率增加（"边缘变亮"效果）
//       → 金属的 F0 取 albedo 颜色，非金属固定 0.04
//   kD = (1 - F) × (1 - metallic)
//       → 能量守恒：被反射的光不参与漫反射
//       → 纯金属 metallic=1 时 kD=0，没有漫反射
// =====================================================================

const float PI = 3.14159265359;

// ----- D: GGX / Trowbridge-Reitz 法线分布 -----
// 返回微面元法线朝向 H 的概率密度
// α = roughness²（迪士尼重映射，让滑条手感线性）
// 公式：D = α² / (π · ((N·H)²·(α²-1) + 1)²)
float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a  = roughness * roughness;    // α = roughness²
    float a2 = a * a;                    // α² = roughness⁴
    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    return a2 / max(denom, 0.0001);     // 防止除零
}

// ----- G₁: Schlick-GGX 单方向几何遮蔽 -----
// k = (roughness+1)² / 8  （直接光照的 k 映射）
// G₁(n,v) = (N·V) / ((N·V)·(1-k) + k)
float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;            // 直接光照用的 k
    return NdotV / (NdotV * (1.0 - k) + k);
}

// ----- G: Smith 几何遮蔽（视线 + 光线两个方向）-----
// G(N,V,L) = G₁(N,V) × G₁(N,L)
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

// ----- F: Fresnel-Schlick 近似 -----
// F(θ) = F0 + (1 - F0) · (1 - cosθ)⁵
// F0 = 垂直入射时的基础反射率
//   非金属：固定 0.04（塑料/石头等）
//   金属：取 albedo 颜色（金银铜等有色反射）
vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// =====================================================================
// 光照计算：对每个光源执行一次完整的 Cook-Torrance BRDF
// =====================================================================

// ---------- 方向光（平行光，无衰减）----------
// 太阳光等无限远光源，所有片元的光线方向相同
vec3 CalcDirectionalLight(LightData light, vec3 N, vec3 V, vec3 albedo,
                          float metallic, float roughness, vec3 F0)
{
    // L = 从表面指向光源 = -光线方向
    vec3 L = normalize(-light.Direction.xyz);
    // H = 半程向量（V 和 L 的中间方向，微面元理论核心）
    vec3 H = normalize(V + L);
    // radiance = 光的颜色 × 强度（方向光无距离衰减）
    vec3 radiance = light.Color.xyz * light.Color.w;

    // 计算 BRDF 三大项
    float NDF = DistributionGGX(N, H, roughness);       // D: 有多少微面朝向 H
    float G   = GeometrySmith(N, V, L, roughness);      // G: 有多少微面未被遮挡
    vec3  F   = FresnelSchlick(max(dot(H, V), 0.0), F0);// F: 反射比例

    // Cook-Torrance 镜面 BRDF: (D × G × F) / (4 × (N·V) × (N·L))
    vec3  numerator   = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3  specular    = numerator / denominator;

    // 能量守恒：被菲涅尔反射的部分不参与漫反射
    // 纯金属 (metallic=1) 完全没有漫反射，只有镜面
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

    // 最终出射辐射度 = (漫反射 + 镜面) × 入射辐射度 × cos(入射角)
    float NdotL = max(dot(N, L), 0.0);
    return (kD * albedo / PI + specular) * radiance * NdotL;
}

// ---------- 点光源（有位置、有半径衰减）----------
vec3 CalcPointLight(LightData light, vec3 N, vec3 V, vec3 worldPos,
                    vec3 albedo, float metallic, float roughness, vec3 F0)
{
    vec3  lightPos = light.Position.xyz;  // 光源世界坐标
    float radius   = light.Position.w;    // 影响半径
    vec3  L        = lightPos - worldPos;  // 从表面到光源的向量
    float distance = length(L);

    // 超出影响半径 → 该光源对此片元无贡献，直接返回
    if (distance > radius)
        return vec3(0.0);

    L = normalize(L);
    vec3 H = normalize(V + L);

    // 平滑衰减函数: attenuation = (1 - (d/r)²)²
    // 比 1/d² 更好：在半径边缘平滑降为 0，无硬边
    float t = clamp(1.0 - (distance * distance) / (radius * radius), 0.0, 1.0);
    float attenuation = t * t;

    // 衰减后的入射辐射度
    vec3 radiance = light.Color.xyz * light.Color.w * attenuation;

    // BRDF 计算（与方向光完全相同）
    float NDF = DistributionGGX(N, H, roughness);
    float G   = GeometrySmith(N, V, L, roughness);
    vec3  F   = FresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3  numerator   = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3  specular    = numerator / denominator;

    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

    float NdotL = max(dot(N, L), 0.0);
    return (kD * albedo / PI + specular) * radiance * NdotL;
}

// =====================================================================
// 主函数 — 逐像素执行
//
// 流程：采样贴图 → 构造表面属性 → 逐光源累加 → 加环境光 → tonemap
// =====================================================================

void main()
{
    // ==================== 1. 采样材质贴图 ====================

    // 基础颜色 = BaseColor因子 × BaseColorMap（如有）
    vec4 albedo4 = u_BaseColor;
    if (u_UseBaseColorMap == 1)
    {
        vec4 texColor = texture(u_BaseColorMap, v_TexCoord);
        // sRGB → 线性空间（BaseColorMap 是 sRGB 编码的 JPG/PNG）
        texColor.rgb = pow(texColor.rgb, vec3(2.2));
        albedo4 *= texColor;
    }
    vec3 albedo = albedo4.rgb;

    // 金属度 & 粗糙度：标量 × 贴图（如有）
    // glTF 规范: MetallicRoughnessMap 的 G 通道=粗糙度, B 通道=金属度
    float metallic  = u_Metallic;
    float roughness = u_Roughness;
    if (u_UseMetallicRoughnessMap == 1)
    {
        vec4 mrSample = texture(u_MetallicRoughnessMap, v_TexCoord);
        roughness *= mrSample.g;  // G 通道 = 粗糙度
        metallic  *= mrSample.b;  // B 通道 = 金属度
    }
    // 钳制粗糙度下限 0.04，防止 GGX 分母趋近于零导致高光爆炸
    roughness = clamp(roughness, 0.04, 1.0);

    // 环境遮蔽：标量 × AOMap 的 R 通道（如有）
    float ao = u_AO;
    if (u_UseAOMap == 1)
        ao *= texture(u_AOMap, v_TexCoord).r;

    // ==================== 2. 构造世界空间法线 ====================

    vec3 N;
    if (u_UseNormalMap == 1)
    {
        // 法线贴图存储的是切线空间法线，范围 [0,1] → 映射回 [-1,1]
        vec3 tangentNormal = texture(u_NormalMap, v_TexCoord).rgb * 2.0 - 1.0;
        // 通过 TBN 矩阵从切线空间变换到世界空间
        N = normalize(v_TBN * tangentNormal);
    }
    else
    {
        // 无法线贴图时直接用顶点法线
        N = normalize(v_Normal);
    }

    // ==================== 3. 视线方向 ====================

    // V = 从当前片元指向相机的单位向量
    vec3 V = normalize(u_ViewPosition.xyz - v_WorldPos);

    // ==================== 4. 基础反射率 F0 ====================

    // 非金属（塑料等）：F0 = 0.04（经验值，大多数介电质的垂直反射率）
    // 金属：F0 = albedo 颜色（金属的反射带有自身颜色）
    // metallic 作为插值权重在两者之间混合
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    // ==================== 5. 逐光源累加直接光照 ====================

    vec3 Lo = vec3(0.0);  // Lo = 所有光源的出射辐射度总和

    // 遍历方向光（当前最多 1 盏）
    for (int i = 0; i < u_NumDirectionalLights; i++)
        Lo += CalcDirectionalLight(u_DirLights[i], N, V, albedo, metallic, roughness, F0);

    // 遍历点光源（当前最多 16 盏）
    for (int i = 0; i < u_NumPointLights; i++)
        Lo += CalcPointLight(u_PointLights[i], N, V, v_WorldPos, albedo, metallic, roughness, F0);

    // 硬编码回退灯光：当场景无灯光时提供默认照明（调试用）
    if (u_NumDirectionalLights == 0 && u_NumPointLights == 0)
    {
        LightData fallback;
        fallback.Direction = vec4(0.0, -1.0, -0.5, 0.0);
        fallback.Color     = vec4(1.0, 1.0, 1.0, 1.0);
        Lo += CalcDirectionalLight(fallback, N, V, albedo, metallic, roughness, F0);
    }

    // ==================== 6. 环境光（临时方案）====================

    // 简单的常数环境项，模拟天空散射光
    // TODO: Phase 后续替换为 IBL（基于图像的光照）— 预滤波环境贴图 + BRDF LUT
    vec3 ambient = vec3(0.03) * albedo * ao;

    // 最终颜色 = 环境光 + 直接光照
    vec3 color = ambient + Lo;

    // ==================== 7. 色调映射 + Gamma 校正（临时）====================

    // Reinhard tonemapping: 把 HDR [0, ∞) 压缩到 [0, 1)
    // 优点：简单，缺点：暗部对比度不够。Phase 5 会换成 ACES + 曝光控制
    color = color / (color + vec3(1.0));

    // Gamma 2.2 校正：线性空间 → sRGB 显示空间
    // 因为所有光照计算都在线性空间，输出到屏幕前必须 gamma 校正
    color = pow(color, vec3(1.0 / 2.2));

    // ==================== 8. 输出 ====================

    o_Color    = vec4(color, albedo4.a);  // RGBA（alpha 用于半透明，当前默认 1.0）
    o_EntityID = u_EntityID;              // 编辑器拾取 ID，写入 RED_INTEGER 附件
}
