// =====================================================================
// RSMDepth.glsl — Reflective Shadow Map 生成（间接光一次弹射）
//
// 作用：
//   从光源视角渲染场景，把每个表面看做一个"虚拟点光源 (VPL)"，
//   把它的出射光通量、世界坐标、世界法线分别写到 3 张 MRT 颜色附件，
//   主 Pass 再随机采样这些 VPL 计算 1-bounce 间接漫反射。
//
//   颜色附件：
//     0 — Flux     (RGBA16F)  rgb = albedo × lightColor × lightIntensity × NdotL
//     1 — WorldPos (RGBA16F)  rgb = 世界空间位置
//     2 — Normal   (RGBA16F)  rgb = 世界空间法线压缩到 [0,1]
//   深度附件：
//     DEPTH32F      gl_FragDepth 由固定管线写入；本 shader 不直接采样
//
// UBO 约定（与主 shader / ShadowDepth 完全一致）：
//   binding 0 — Camera    : 主要用 LightSpaceMatrix
//   binding 1 — Transform : 逐物体模型矩阵
//   binding 2 — Material  : BaseColor + texture flags（取 albedo）
//   binding 3 — Lights    : 取第一盏方向光的 Direction / Color
//
// 纹理槽位（与主 shader 保持一致，UploadMaterial 会绑好）：
//   slot 3 — BaseColorMap
// =====================================================================

// =========================== 顶点着色器 =============================
#type vertex
#version 450 core

// ---- 顶点属性（与 MeshVertex 布局一致）----
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TexCoord;

// ---- Camera UBO (binding = 0) ----
layout(std140, binding = 0) uniform Camera
{
    mat4 u_ViewProjection;
    vec4 u_ViewPosition;
    mat4 u_LightSpaceMatrix;
    vec4 u_ShadowParams;
    vec4 u_ShadowParams2;
    mat4 u_View;
};

// ---- Transform UBO (binding = 1) ----
layout(std140, binding = 1) uniform Transform
{
    mat4 u_Transform;
};

// 传给片元着色器
layout(location = 0) out vec3 v_WorldPos;
layout(location = 1) out vec3 v_WorldNormal;
layout(location = 2) out vec2 v_TexCoord;

void main()
{
    vec4 worldPos = u_Transform * vec4(a_Position, 1.0);
    v_WorldPos    = worldPos.xyz;

    // 用法线矩阵处理非均匀缩放；和主 shader 一致
    mat3 normalMatrix = mat3(transpose(inverse(u_Transform)));
    v_WorldNormal     = normalize(normalMatrix * a_Normal);

    v_TexCoord    = a_TexCoord;
    gl_Position   = u_LightSpaceMatrix * worldPos;
}

// =========================== 片元着色器 =============================
#type fragment
#version 450 core

layout(location = 0) in vec3 v_WorldPos;
layout(location = 1) in vec3 v_WorldNormal;
layout(location = 2) in vec2 v_TexCoord;

// ---- Material UBO (binding = 2) ----
// 字段顺序必须与 C++ 端 Renderer3DData::MaterialData 一致。
layout(std140, binding = 2) uniform Material
{
    vec4  u_BaseColor;
    float u_Metallic;
    float u_Roughness;
    float u_AO;
    int   u_UseBaseColorMap;
    int   u_UseNormalMap;
    int   u_UseMetallicRoughnessMap;
    int   u_UseAOMap;
    int   u_EntityID;
    int   u_UseAlphaMap;
    float u_AlphaCutoff;
    int   u_AlphaMode;
    int   _matPad0;
};

// ---- Lights UBO (binding = 3) ----
struct LightData
{
    vec4 Direction;
    vec4 Color;
    vec4 Position;
    int  CastShadow;
    int  _pad0;
    int  _pad1;
    int  _pad2;
};

layout(std140, binding = 3) uniform Lights
{
    int       u_NumDirectionalLights;
    int       u_NumPointLights;
    int       _lightPad0;
    int       _lightPad1;
    LightData u_DirLights[1];
    LightData u_PointLights[16];
};

// 主 shader 约定槽位：BaseColorMap = slot 3
layout(binding = 3) uniform sampler2D u_BaseColorMap;

// MRT 输出
layout(location = 0) out vec4 o_Flux;      // 出射光通量
layout(location = 1) out vec4 o_WorldPos;  // 世界坐标
layout(location = 2) out vec4 o_Normal;    // 世界法线（压到 [0,1]）

void main()
{
    // ---- albedo（与主 shader 保持一致：sRGB → Linear）----
    vec3 albedo = u_BaseColor.rgb;
    if (u_UseBaseColorMap == 1)
    {
        vec4 texColor = texture(u_BaseColorMap, v_TexCoord);
        texColor.rgb  = pow(texColor.rgb, vec3(2.2));
        albedo       *= texColor.rgb;
    }

    // ---- 取第一盏方向光（无方向光时输出全 0，主 Pass 自然得到 0 间接光）----
    vec3 lightDir   = vec3(0.0, -1.0, 0.0);
    vec3 lightColor = vec3(0.0);
    if (u_NumDirectionalLights > 0)
    {
        lightDir   = u_DirLights[0].Direction.xyz;
        lightColor = u_DirLights[0].Color.xyz * u_DirLights[0].Color.w;
    }

    // L = 表面 → 光源
    vec3 L     = normalize(-lightDir);
    vec3 N     = normalize(v_WorldNormal);
    float NdL  = max(dot(N, L), 0.0);

    // 简化：用 Lambert 漫反射近似出射光通量
    // Flux = albedo × lightColor × NdotL
    o_Flux     = vec4(albedo * lightColor * NdL, 1.0);
    o_WorldPos = vec4(v_WorldPos, 1.0);
    o_Normal   = vec4(N * 0.5 + 0.5, 1.0);
}
