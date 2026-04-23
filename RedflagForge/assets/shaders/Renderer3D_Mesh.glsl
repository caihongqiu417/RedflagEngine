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
    mat4 u_LightSpaceMatrix;    // 方向光的 Ortho × View；Shadow Pass 和主 Pass 共用
    vec4 u_ShadowParams;        // x=depthBias y=normalBias z=invMapSize w=mode(0=off,1=PCF,2=PCSS)
    vec4 u_ShadowParams2;       // x=pcfRadius y=lightSize z=viewportW w=viewportH
    mat4 u_View;                // 主相机 view 矩阵（SSAO 用，主 shader 未用到）
};

// ---- Transform UBO (binding = 1) ----
// 由 Renderer3D::EndScene 在每次 DrawCall 前上传当前物体的模型矩阵
layout(std140, binding = 1) uniform Transform
{
    mat4 u_Transform;
};

// ---- 传递给片元着色器的插值变量 ----
layout(location = 0) out vec3  v_WorldPos;  // 世界空间位置（用于光照距离计算）
layout(location = 1) out vec2  v_TexCoord;  // UV 坐标
layout(location = 2) out vec3  v_Normal;    // 世界空间法线（无法线贴图时的后备）
layout(location = 3) out mat3  v_TBN;       // 切线空间 → 世界空间 的变换矩阵
                                            // mat3 占用 location 3, 4, 5
layout(location = 6) out vec4  v_LightSpacePos; // 光源裁剪空间位置（主 Pass 用来查 Shadow Map）

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

    // 7. 光源裁剪空间坐标（主 Pass 做 Shadow Map 查询时使用）
    //    注意：我们不做 perspective divide，留给片元阶段处理，
    //    这样可以利用硬件对 w 分量的透视正确插值。
    v_LightSpacePos = u_LightSpaceMatrix * worldPos;

    // 8. 最终裁剪空间位置
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
layout(location = 6) in vec4  v_LightSpacePos;

// ---- Camera UBO (binding = 0) ----
layout(std140, binding = 0) uniform Camera
{
    mat4 u_ViewProjection;
    vec4 u_ViewPosition;
    mat4 u_LightSpaceMatrix;
    vec4 u_ShadowParams;       // x=depthBias y=normalBias z=invMapSize w=mode
    vec4 u_ShadowParams2;      // x=pcfRadius y=lightSize z=viewportW w=viewportH
    mat4 u_View;
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
    int   u_UseAlphaMap;             // 1 = 启用独立 alpha / opacity 贴图
    float u_AlphaCutoff;             // Mask 模式下的裁剪阈值
    int   u_AlphaMode;               // 0=Opaque, 1=Mask, 2=Blend
    int   _matPad0;
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
layout(binding = 10) uniform sampler2D u_AlphaMap;             // slot 10: R=alpha / opacity

// ---- Shadow Map（方向光，slot 11）----
// 由 SceneRenderer 在 Shadow Pass 之后绑定；DEPTH32F 单通道纹理。
// 过滤设成 NEAREST（见 OpenGLFramebuffer::AttachDepthTexture），
// PCF 采样手动在 shader 里做，避免硬件插值把不连续深度拉扯出错误阴影。
layout(binding = 11) uniform sampler2D u_ShadowMap;

// ---- SSAO Map（slot 12）----
// 由 SceneRenderer::RunSSAOPass 生成，单通道（读 .r）。屏幕空间纹理，
// 需要用 gl_FragCoord / viewportSize 做 UV。关闭 SSAO 时 SceneRenderer
// 会绑定 1×1 全白纹理，读到 ao=1（无衰减）。
layout(binding = 12) uniform sampler2D u_SSAOMap;

// ======================= IBL 纹理（Split Sum 预计算）=======================
// 这三张纹理由 IBLPrecompute::Init() 在程序启动时生成，运行时只需采样。
// 对应课上讲义里 Split Sum 的两个积分项：
//   u_IrradianceMap → 漫反射半球积分（"平均入射光强度"）
//   u_PrefilterMap  → 按 roughness 分 mip 存的预滤波镜面环境图
//   u_BRDFLUT       → (NdotV, roughness) 索引的 Fresnel 积分查找表
layout(binding = 7) uniform samplerCube u_IrradianceMap;   // slot 7
layout(binding = 8) uniform samplerCube u_PrefilterMap;    // slot 8 （含 mip chain）
layout(binding = 9) uniform sampler2D   u_BRDFLUT;         // slot 9 （RG16F）
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

// ----- IBL 专用 Fresnel：考虑粗糙度对掠射角反射的抑制 -----
//
// 为什么需要这个变体？
//   标准 FresnelSchlick 在掠射角时会趋向 vec3(1.0)（全反射），
//   但粗糙表面不应该有那么强的掠射反射。
//   这里用 max(vec3(1-roughness), F0) 作为上限，
//   roughness 越大 → 掠射反射越弱，视觉上更自然。
vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0)
             * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// =====================================================================
// 光照计算：对每个光源执行一次完整的 Cook-Torrance BRDF
// =====================================================================

// =====================================================================
// Shadow Map 采样
//
// 约定：
//   v_LightSpacePos = u_LightSpaceMatrix * vec4(worldPos, 1)
//   执行透视除法后映射到 [0,1]×[0,1] 纹理坐标 + [0,1] 深度参考值
//
// 提供两种过滤策略（由 u_ShadowParams.w 选择）：
//   0  关闭阴影（返回 1，即完全亮）
//   1  PCF 3×3：9 次采样 + 等权重平均；标准、稳健
//   2  PCSS：先做 blocker search 估算平均遮挡深度，
//            再按 (receiver - blocker)/blocker × lightSize 动态
//            扩大 PCF 核半径，实现越远越软的接触硬化阴影
// =====================================================================

// 单次比较采样：1 = 可见，0 = 遮挡
float SampleShadow(vec2 uv, float refDepth)
{
    float occ = texture(u_ShadowMap, uv).r;
    return refDepth <= occ ? 1.0 : 0.0;
}

// 3×3 PCF（9 taps，等权重）
float PCF3x3(vec2 baseUV, float refDepth, float texel)
{
    float sum = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            sum += SampleShadow(baseUV + vec2(x, y) * texel, refDepth);
    return sum / 9.0;
}

// 任意核半径 PCF（radius 单位为纹素；供 PCSS 动态调整 kernel 用）
// 固定采 5×5 = 25 taps，视觉上对半影过渡更平滑。
float PCFVariable(vec2 baseUV, float refDepth, float radius, float texel)
{
    float sum = 0.0;
    float samples = 0.0;
    for (int y = -2; y <= 2; ++y)
    {
        for (int x = -2; x <= 2; ++x)
        {
            vec2 offset = vec2(x, y) * (radius * 0.5) * texel;
            sum     += SampleShadow(baseUV + offset, refDepth);
            samples += 1.0;
        }
    }
    return sum / samples;
}

// Blocker search（PCSS 第一步）
//   在固定半径里找比接收点更近的样本，计算它们的平均深度。
//   若没有任何遮挡者，返回 -1，调用方直接判定全亮。
float FindBlockerDepth(vec2 baseUV, float refDepth, float searchRadius, float texel)
{
    float blockerSum = 0.0;
    float blockers   = 0.0;
    const int N = 2;   // 5×5 搜索核
    for (int y = -N; y <= N; ++y)
    {
        for (int x = -N; x <= N; ++x)
        {
            vec2 offset = vec2(x, y) * (searchRadius / float(N)) * texel;
            float d = texture(u_ShadowMap, baseUV + offset).r;
            if (d < refDepth)
            {
                blockerSum += d;
                blockers   += 1.0;
            }
        }
    }
    if (blockers < 1.0) return -1.0;
    return blockerSum / blockers;
}

// 计算方向光的可见度 (1 = 完全可见, 0 = 完全遮挡)
//   lightSpacePos 已经做了 u_LightSpaceMatrix × worldPos，但还没除 w
//   N / L 用来做斜率相关的 normal bias，避免 shadow acne
float ComputeDirectionalShadow(vec4 lightSpacePos, vec3 N, vec3 L)
{
    int mode = int(u_ShadowParams.w);
    if (mode == 0) return 1.0;   // 阴影关闭

    // --- 透视除法 → NDC → [0,1] ---
    vec3 proj = lightSpacePos.xyz / lightSpacePos.w;
    proj = proj * 0.5 + 0.5;

    // 超出 Shadow Map 视锥：当作完全照亮（避免画面边缘变黑）
    if (proj.z > 1.0 || proj.z < 0.0)                return 1.0;
    if (any(lessThan(proj.xy, vec2(0.0))) ||
        any(greaterThan(proj.xy, vec2(1.0))))         return 1.0;

    float texel      = u_ShadowParams.z;                 // 1.0 / mapSize
    float depthBias  = u_ShadowParams.x;
    float normalBias = u_ShadowParams.y;

    // 斜率相关 bias：光线越斜入，bias 越大，消除 shadow acne 条纹
    float NdotL = max(dot(N, L), 0.0);
    float bias  = max(depthBias * (1.0 - NdotL), depthBias * 0.1);
    bias       += normalBias * texel;  // 额外再按纹素做 normal bias
    float refDepth = proj.z - bias;

    if (mode == 2)
    {
        // ---- PCSS ----
        // 由 ImGui 调节：虚拟光源尺寸（纹素单位）越大半影越宽
        float LIGHT_SIZE   = max(u_ShadowParams2.y, 0.0);
        const float SEARCH_RADIUS = 4.0; // blocker search 固定半径

        float avgBlocker = FindBlockerDepth(proj.xy, refDepth, SEARCH_RADIUS, texel);
        if (avgBlocker < 0.0) return 1.0;  // 没有遮挡者 → 全亮

        // 接触硬化：遮挡者距离接收点越近，半影越窄
        float penumbra = (refDepth - avgBlocker) / max(avgBlocker, 0.0001);
        float radius   = clamp(penumbra * LIGHT_SIZE, 0.5, 16.0);
        return PCFVariable(proj.xy, refDepth, radius, texel);
    }

    // ---- 默认 PCF ----
    // 用 u_ShadowParams2.x 作为可调核半径（纹素）。过小时退化为固定 3×3。
    float pcfRadius = u_ShadowParams2.x;
    if (pcfRadius <= 1.001)
        return PCF3x3(proj.xy, refDepth, texel);
    return PCFVariable(proj.xy, refDepth, pcfRadius, texel);
}

// ---------- 方向光（平行光，无衰减）----------
// 太阳光等无限远光源，所有片元的光线方向相同
// shadow 传入 [0,1]：1=完全照亮，0=完全遮挡。只对直接光部分衰减，
// 环境光 / IBL 不受影响（物理上 shadow map 不能约束半球环境贡献）。
vec3 CalcDirectionalLight(LightData light, vec3 N, vec3 V, vec3 albedo,
                          float metallic, float roughness, vec3 F0, float shadow)
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

    // 高光爆炸抑制（UE4 风格能量钳制）：
    // 方向光是 delta 光源（立体角=0），碰上低粗糙度会让 NDF 发散到数十万，
    // 导致镜面类表面整块爆白。物理上真实太阳有一定立体角，这里用硬钳制近似。
    specular = min(specular, vec3(80.0));

    // 能量守恒：被菲涅尔反射的部分不参与漫反射
    // 纯金属 (metallic=1) 完全没有漫反射，只有镜面
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

    // 最终出射辐射度 = (漫反射 + 镜面) × 入射辐射度 × cos(入射角) × 可见度
    float NdotL = max(dot(N, L), 0.0);
    return (kD * albedo / PI + specular) * radiance * NdotL * shadow;
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
    specular = min(specular, vec3(80.0));   // 与方向光相同的爆炸抑制

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

    // ==================== 1.5 Alpha 裁剪 ====================
    // 独立 AlphaMap 或 BaseColorMap 的 alpha 通道 → 可能用于钟面玻璃 / 植物叶片等裁剪
    float alphaValue = albedo4.a;
    if (u_UseAlphaMap == 1)
        alphaValue *= texture(u_AlphaMap, v_TexCoord).r;
    if (u_AlphaMode == 1 && alphaValue < u_AlphaCutoff)
        discard; // Mask 模式：低于阈值直接丢弃片元，让钟面玻璃镂空

    // 非 Blend 模式下，输出 alpha 强制为 1.0，避免 FBX 脏 BaseColor.a 或
    // Renderer2D 启用的 alpha 混合把模型整体擦成透明。
    float outAlpha = (u_AlphaMode == 2) ? alphaValue : 1.0;

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
    // 钳制粗糙度下限 0.089（Filament 推荐值），防止 GGX 分母趋近于零导致
    // 方向光高光爆炸（太阳 delta 光源 × 超尖 NDF → 整块镜面饱和白）。
    // 0.04 过低在 metallic=1 的平面镜上仍然会整块爆白。
    roughness = clamp(roughness, 0.089, 1.0);

    // 环境遮蔽：标量 × AOMap 的 R 通道（如有）
    float ao = u_AO;
    if (u_UseAOMap == 1)
        ao *= texture(u_AOMap, v_TexCoord).r;

    // 屏幕空间 AO：把 SSAO Pass 的结果乘到材质 AO 上，只影响 ambient。
    // 关闭时 SceneRenderer 绑定的是 1×1 白纹理，ssao=1 没有副作用。
    // ShadowParams2.zw = viewport 尺寸（像素），用于把 gl_FragCoord 转 UV。
    vec2 vpSize = max(u_ShadowParams2.zw, vec2(1.0));
    float ssao  = texture(u_SSAOMap, gl_FragCoord.xy / vpSize).r;
    ao *= ssao;

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
    //   对唯一的 "主方向光" 做 Shadow Map 查询；多方向光场景这里
    //   只对第 0 盏起作用，其余盏退化成无阴影（当前 MAX_DIRECTIONAL_LIGHTS=1，
    //   所以完全等价）。
    for (int i = 0; i < u_NumDirectionalLights; i++)
    {
        vec3 L = normalize(-u_DirLights[i].Direction.xyz);
        float shadow = (i == 0)
            ? ComputeDirectionalShadow(v_LightSpacePos, N, L)
            : 1.0;
        Lo += CalcDirectionalLight(u_DirLights[i], N, V, albedo, metallic, roughness, F0, shadow);
    }

    // 遍历点光源（当前最多 16 盏）
    for (int i = 0; i < u_NumPointLights; i++)
        Lo += CalcPointLight(u_PointLights[i], N, V, v_WorldPos, albedo, metallic, roughness, F0);

    // 硬编码回退灯光：当场景无灯光时提供默认照明（调试用）
    if (u_NumDirectionalLights == 0 && u_NumPointLights == 0)
    {
        LightData fallback;
        fallback.Direction = vec4(0.0, -1.0, -0.5, 0.0);
        fallback.Color     = vec4(1.0, 1.0, 1.0, 1.0);
        Lo += CalcDirectionalLight(fallback, N, V, albedo, metallic, roughness, F0, 1.0);
    }

    // ==================== 6. IBL 环境光（Split Sum 近似）====================
    //
    // 对应课上讲义：
    //   原始环境光积分不可实时计算，Split Sum 把它拆成两个可预计算的部分：
    //     (1) 漫反射 = irradiance(N) × albedo
    //     (2) 镜面   = prefilteredEnv(R, roughness) × BRDF_LUT(NdotV, roughness)
    //   运行时只需 3 次纹理采样 + 乘加即可还原完整环境光积分。
    //
    // u_ViewPosition.w 被复用为 IBL 就绪标志：
    //   1.0 = IBL 已预计算完成 → 使用 IBL 环境光
    //   0.0 = IBL 未加载       → 回退到常数 0.03

    vec3 ambient;

    if (u_ViewPosition.w > 0.5)
    {
        // ------- Fresnel（IBL 专用版，考虑 roughness）-------
        // 表面镜面反射与漫反射的能量分配系数
        vec3 kS = FresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
        // 金属没有漫反射：metallic=1 → kD=0
        vec3 kD = (1.0 - kS) * (1.0 - metallic);

        // ------- 漫反射 IBL -------
        // 用法线 N 采样辐照度图，得到 N 方向上整个半球的积分辐照度
        // 对应课上讲义："平均入射光强度"
        vec3 irradiance = texture(u_IrradianceMap, N).rgb;
        vec3 diffuseIBL = irradiance * albedo;

        // ------- 镜面 IBL（Split Sum 核心）-------
        // 反射方向 R 决定采样环境图的哪个方向
        vec3 R = reflect(-V, N);

        // roughness → mip 层级（roughness 越大 → mip 越高 → 越模糊）
        // MAX_REFLECTION_LOD = 4.0 对应预计算的 5 个 mip level (0~4)
        const float MAX_REFLECTION_LOD = 4.0;
        // textureLod 显式指定 mip 层级采样，实现"按 roughness 选 MipMap 层"
        vec3 prefilteredColor = textureLod(u_PrefilterMap, R, roughness * MAX_REFLECTION_LOD).rgb;

        // 查 BRDF LUT：(NdotV, roughness) → (Fresnel scale, Fresnel bias)
        // 对应课上右侧公式拆出的两个积分项：
        //   brdf.x = ∫ (f_r/F)[1-(1-cosθ)⁵] cosθ dω  → scale
        //   brdf.y = ∫ (f_r/F)  (1-cosθ)⁵   cosθ dω  → bias
        vec2 brdf = texture(u_BRDFLUT, vec2(max(dot(N, V), 0.0), roughness)).rg;
        // 最终镜面 IBL = 预滤波颜色 × (F0 × scale + bias)
        vec3 specularIBL = prefilteredColor * (kS * brdf.x + brdf.y);

        // 合成环境光 = (漫反射 + 镜面) × AO
        ambient = (kD * diffuseIBL + specularIBL) * ao;
    }
    else
    {
        // IBL 未就绪时的常数回退（与原始实现一致）
        ambient = vec3(0.03) * albedo * ao;
    }

    // 最终颜色 = 环境光 + 直接光照
    vec3 color = ambient + Lo;

    // ==================== 7. 输出（HDR，不做 tonemap）====================
    //
    // Tonemap（ACES）和 Gamma 校正已迁移到后处理阶段（PostProcess_Composite.glsl）。
    // 原因：Bloom 需要先从 HDR buffer 提取高光（亮度 > threshold），
    //       如果在这里就做 tonemap，高光信息被压缩到 [0,1] 就无法准确提取了。
    // 主 FBO attachment0 现在是 RGBA16F，可存储超过 1.0 的 HDR 值。

    o_Color    = vec4(color, outAlpha);   // HDR RGBA，Bloom / Tonemap 后处理会消费这张纹理
    o_EntityID = u_EntityID;              // 编辑器拾取 ID，写入 RED_INTEGER 附件（不受影响）
}
