// =====================================================================
// Renderer3D_Mesh_Instanced.glsl — 实例化 PBR 着色器
//
// 与 Renderer3D_Mesh.glsl 的区别：
//   - 无 Transform UBO (binding 1)、无 Material UBO (binding 2)
//   - Transform / BaseColor / MatParams / EntityID 通过实例属性传入
//   - 不支持贴图（仅面向无贴图的纯色 Mesh，由 EndScene 筛选后调用）
//   - 其余 Camera UBO / Light UBO / IBL / Shadow / SSAO 与原 shader 完全相同
// =====================================================================

// =========================== 顶点着色器 =============================
#type vertex
#version 450 core

// ---- 逐顶点属性 ----
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TexCoord;
layout(location = 3) in vec3 a_Tangent;

// ---- 逐实例属性 (divisor = 1) ----
layout(location = 4) in mat4 a_Transform;   // locations 4,5,6,7
layout(location = 8) in vec4 a_BaseColor;   // location 8
layout(location = 9) in vec4 a_MatParams;   // location 9: x=metallic, y=roughness, z=ao
layout(location = 10) in int  a_EntityID;   // location 10

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

// ---- 传递给片元着色器 ----
layout(location = 0) out vec3  v_WorldPos;
layout(location = 1) out vec2  v_TexCoord;
layout(location = 2) out vec3  v_Normal;
layout(location = 3) out mat3  v_TBN;
layout(location = 6) out vec4  v_LightSpacePos;
layout(location = 7) flat out vec4 v_BaseColor;
layout(location = 8) flat out vec4 v_MatParams;
layout(location = 9) flat out int  v_EntityID;

void main()
{
    vec4 worldPos = a_Transform * vec4(a_Position, 1.0);
    v_WorldPos  = worldPos.xyz;
    v_TexCoord  = a_TexCoord;

    mat3 normalMatrix = mat3(transpose(inverse(a_Transform)));
    vec3 N = normalize(normalMatrix * a_Normal);
    vec3 T = normalize(normalMatrix * a_Tangent);
    T = normalize(T - dot(T, N) * N);
    vec3 B = cross(N, T);

    v_Normal        = N;
    v_TBN           = mat3(T, B, N);
    v_LightSpacePos = u_LightSpaceMatrix * worldPos;

    v_BaseColor = a_BaseColor;
    v_MatParams = a_MatParams;
    v_EntityID  = a_EntityID;

    gl_Position = u_ViewProjection * worldPos;
}

// =========================== 片元着色器 =============================
#type fragment
#version 450 core

layout(location = 0) out vec4 o_Color;
layout(location = 1) out int  o_EntityID;

layout(location = 0) in vec3  v_WorldPos;
layout(location = 1) in vec2  v_TexCoord;
layout(location = 2) in vec3  v_Normal;
layout(location = 3) in mat3  v_TBN;
layout(location = 6) in vec4  v_LightSpacePos;
layout(location = 7) flat in vec4 v_BaseColor;
layout(location = 8) flat in vec4 v_MatParams;
layout(location = 9) flat in int  v_EntityID;

// ---- Camera UBO ----
layout(std140, binding = 0) uniform Camera
{
    mat4 u_ViewProjection;
    vec4 u_ViewPosition;
    mat4 u_LightSpaceMatrix;
    vec4 u_ShadowParams;
    vec4 u_ShadowParams2;
    mat4 u_View;
};

// ---- Material UBO (binding = 2) — per-batch 贴图开关 ----
// 标量字段（BaseColor / Metallic 等）来自逐实例属性，不从这里读取。
// UBO 布局与非实例化 shader 完全相同，保证 C++ 端共用同一个 MaterialData struct。
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

// ---- 贴图采样器（槽位与非实例化 shader 完全相同）----
layout(binding = 3)  uniform sampler2D u_BaseColorMap;
layout(binding = 4)  uniform sampler2D u_NormalMap;
layout(binding = 5)  uniform sampler2D u_MetallicRoughnessMap;
layout(binding = 6)  uniform sampler2D u_AOMap;
layout(binding = 10) uniform sampler2D u_AlphaMap;

// ---- Lights UBO (binding = 3) ----
struct LightData
{
    vec4 Direction;
    vec4 Color;
    vec4 Position;
    int  CastShadow;
    int  _pad0; int _pad1; int _pad2;
};
layout(std140, binding = 3) uniform Lights
{
    int       u_NumDirectionalLights;
    int       u_NumPointLights;
    int       _lightPad0; int _lightPad1;
    LightData u_DirLights[1];
    LightData u_PointLights[16];
};

// ---- Shadow Map & SSAO ----
layout(binding = 11) uniform sampler2D u_ShadowMap;
layout(binding = 12) uniform sampler2D u_SSAOMap;

// ---- IBL ----
layout(binding = 7) uniform samplerCube u_IrradianceMap;
layout(binding = 8) uniform samplerCube u_PrefilterMap;
layout(binding = 9) uniform sampler2D   u_BRDFLUT;

// =====================================================================
// PBR 辅助函数（与 Renderer3D_Mesh.glsl 完全相同）
// =====================================================================
const float PI = 3.14159265359;

float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a  = roughness * roughness;
    float a2 = a * a;
    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom  = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    return a2 / max(denom, 0.0001);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0)
             * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// ---- Shadow 采样（与原 shader 完全相同）----
float SampleShadow(vec2 uv, float refDepth)
{
    return refDepth <= texture(u_ShadowMap, uv).r ? 1.0 : 0.0;
}
float PCF3x3(vec2 baseUV, float refDepth, float texel)
{
    float sum = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            sum += SampleShadow(baseUV + vec2(x, y) * texel, refDepth);
    return sum / 9.0;
}
float PCFVariable(vec2 baseUV, float refDepth, float radius, float texel)
{
    float sum = 0.0, samples = 0.0;
    for (int y = -2; y <= 2; ++y)
        for (int x = -2; x <= 2; ++x)
        {
            sum += SampleShadow(baseUV + vec2(x, y) * (radius * 0.5) * texel, refDepth);
            samples += 1.0;
        }
    return sum / samples;
}
float FindBlockerDepth(vec2 baseUV, float refDepth, float searchRadius, float texel)
{
    float blockerSum = 0.0, blockers = 0.0;
    const int N = 2;
    for (int y = -N; y <= N; ++y)
        for (int x = -N; x <= N; ++x)
        {
            float d = texture(u_ShadowMap, baseUV + vec2(x, y) * (searchRadius / float(N)) * texel).r;
            if (d < refDepth) { blockerSum += d; blockers += 1.0; }
        }
    if (blockers < 1.0) return -1.0;
    return blockerSum / blockers;
}
float ComputeDirectionalShadow(vec4 lightSpacePos, vec3 N, vec3 L)
{
    int mode = int(u_ShadowParams.w);
    if (mode == 0) return 1.0;

    vec3 proj = lightSpacePos.xyz / lightSpacePos.w;
    proj = proj * 0.5 + 0.5;
    if (proj.z > 1.0 || proj.z < 0.0)                          return 1.0;
    if (any(lessThan(proj.xy, vec2(0.0))) ||
        any(greaterThan(proj.xy, vec2(1.0))))                   return 1.0;

    float texel     = u_ShadowParams.z;
    float depthBias = u_ShadowParams.x, normalBias = u_ShadowParams.y;
    float NdotL     = max(dot(N, L), 0.0);
    float bias      = max(depthBias * (1.0 - NdotL), depthBias * 0.1) + normalBias * texel;
    float refDepth  = proj.z - bias;

    if (mode == 2)
    {
        float avgBlocker = FindBlockerDepth(proj.xy, refDepth, 4.0, texel);
        if (avgBlocker < 0.0) return 1.0;
        float penumbra = (refDepth - avgBlocker) / max(avgBlocker, 0.0001);
        float radius   = clamp(penumbra * max(u_ShadowParams2.y, 0.0), 0.5, 16.0);
        return PCFVariable(proj.xy, refDepth, radius, texel);
    }
    float pcfRadius = u_ShadowParams2.x;
    return (pcfRadius <= 1.001) ? PCF3x3(proj.xy, refDepth, texel)
                                 : PCFVariable(proj.xy, refDepth, pcfRadius, texel);
}

// ---- 直接光照（与原 shader 完全相同）----
vec3 CalcDirectionalLight(LightData light, vec3 N, vec3 V, vec3 albedo,
                          float metallic, float roughness, vec3 F0, float shadow)
{
    vec3 L = normalize(-light.Direction.xyz);
    vec3 H = normalize(V + L);
    vec3 radiance = light.Color.xyz * light.Color.w;
    float NDF = DistributionGGX(N, H, roughness);
    float G   = GeometrySmith(N, V, L, roughness);
    vec3  F   = FresnelSchlick(max(dot(H, V), 0.0), F0);
    vec3  specular = (NDF * G * F) / (4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001);
    specular = min(specular, vec3(80.0));   // 高光爆炸抑制（UE4 风格能量钳制）
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    return (kD * albedo / PI + specular) * radiance * max(dot(N, L), 0.0) * shadow;
}
vec3 CalcPointLight(LightData light, vec3 N, vec3 V, vec3 worldPos,
                    vec3 albedo, float metallic, float roughness, vec3 F0)
{
    vec3  lightPos = light.Position.xyz;
    float radius   = light.Position.w;
    vec3  L        = lightPos - worldPos;
    float distance = length(L);
    if (distance > radius) return vec3(0.0);
    L = normalize(L);
    vec3 H = normalize(V + L);
    float t = clamp(1.0 - (distance * distance) / (radius * radius), 0.0, 1.0);
    vec3 radiance = light.Color.xyz * light.Color.w * t * t;
    float NDF = DistributionGGX(N, H, roughness);
    float G   = GeometrySmith(N, V, L, roughness);
    vec3  F   = FresnelSchlick(max(dot(H, V), 0.0), F0);
    vec3  specular = (NDF * G * F) / (4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001);
    specular = min(specular, vec3(80.0));   // 高光爆炸抑制
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    return (kD * albedo / PI + specular) * radiance * max(dot(N, L), 0.0);
}

// =====================================================================
void main()
{
    // 基础颜色 = 逐实例 tint × BaseColorMap（如有）
    vec4 albedo4 = v_BaseColor;
    if (u_UseBaseColorMap == 1)
    {
        vec4 tc = texture(u_BaseColorMap, v_TexCoord);
        tc.rgb   = pow(tc.rgb, vec3(2.2));   // sRGB → 线性
        albedo4 *= tc;
    }
    vec3 albedo = albedo4.rgb;

    // 金属度 / 粗糙度：逐实例标量 × 贴图（如有）
    float metallic  = v_MatParams.x;
    float roughness = v_MatParams.y;
    float ao        = v_MatParams.z;
    if (u_UseMetallicRoughnessMap == 1)
    {
        vec4 mr  = texture(u_MetallicRoughnessMap, v_TexCoord);
        roughness *= mr.g;
        metallic  *= mr.b;
    }
    roughness = clamp(roughness, 0.089, 1.0);

    if (u_UseAOMap == 1)
        ao *= texture(u_AOMap, v_TexCoord).r;

    // SSAO
    vec2 vpSize = max(u_ShadowParams2.zw, vec2(1.0));
    ao *= texture(u_SSAOMap, gl_FragCoord.xy / vpSize).r;

    // 法线：法线贴图（如有）或顶点法线
    vec3 N;
    if (u_UseNormalMap == 1)
    {
        vec3 tn = texture(u_NormalMap, v_TexCoord).rgb * 2.0 - 1.0;
        N = normalize(v_TBN * tn);
    }
    else
    {
        N = normalize(v_Normal);
    }

    vec3 V = normalize(u_ViewPosition.xyz - v_WorldPos);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    // 直接光照
    vec3 Lo = vec3(0.0);
    for (int i = 0; i < u_NumDirectionalLights; i++)
    {
        vec3 L = normalize(-u_DirLights[i].Direction.xyz);
        float shadow = (i == 0) ? ComputeDirectionalShadow(v_LightSpacePos, N, L) : 1.0;
        Lo += CalcDirectionalLight(u_DirLights[i], N, V, albedo, metallic, roughness, F0, shadow);
    }
    for (int i = 0; i < u_NumPointLights; i++)
        Lo += CalcPointLight(u_PointLights[i], N, V, v_WorldPos, albedo, metallic, roughness, F0);

    if (u_NumDirectionalLights == 0 && u_NumPointLights == 0)
    {
        LightData fallback;
        fallback.Direction = vec4(0.0, -1.0, -0.5, 0.0);
        fallback.Color     = vec4(1.0, 1.0, 1.0, 1.0);
        Lo += CalcDirectionalLight(fallback, N, V, albedo, metallic, roughness, F0, 1.0);
    }

    // IBL 环境光
    vec3 ambient;
    if (u_ViewPosition.w > 0.5)
    {
        vec3 kS = FresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
        vec3 kD = (1.0 - kS) * (1.0 - metallic);
        vec3 irradiance    = texture(u_IrradianceMap, N).rgb;
        vec3 R             = reflect(-V, N);
        vec3 prefilteredColor = textureLod(u_PrefilterMap, R, roughness * 4.0).rgb;
        vec2 brdf          = texture(u_BRDFLUT, vec2(max(dot(N, V), 0.0), roughness)).rg;
        ambient = (kD * irradiance * albedo + prefilteredColor * (kS * brdf.x + brdf.y)) * ao;
    }
    else
    {
        ambient = vec3(0.03) * albedo * ao;
    }

    o_Color    = vec4(ambient + Lo, 1.0);
    o_EntityID = v_EntityID;
}
