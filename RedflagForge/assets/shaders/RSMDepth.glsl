/*
 * 为计算一次反射光，记录灯光照到的表面信息。
 * 除了阴影所需的深度，还记录表面位置、朝向和反射光的颜色强度。
 * 后续把这些受光表面当作许多小光源，估算它们能照亮周围多少区域。
 * 这里使用第一盏方向光。颜色贴图决定反射的颜色，只记录朝向灯光的一面。
 * 镂空表面沿用主画面的透明度裁剪，不让透明区域成为反射光源。
 */

// ---- 顶点阶段 ----
#type vertex
#version 450 core

// 顶点属性（与 MeshVertex 布局一致）
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

    // 用法线矩阵处理非均匀缩放，和主 shader 一致
    mat3 normalMatrix = mat3(transpose(inverse(u_Transform)));
    v_WorldNormal     = normalize(normalMatrix * a_Normal);

    v_TexCoord    = a_TexCoord;
    gl_Position   = u_LightSpaceMatrix * worldPos;
}

// ---- 片元阶段 ----
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
layout(binding = 5) uniform sampler2D u_MetallicRoughnessMap;
layout(binding = 10) uniform sampler2D u_AlphaMap;

// MRT 输出
layout(location = 0) out vec4 o_Flux;      // 每单位光源投影面积的反射通量
layout(location = 1) out vec4 o_WorldPos;  // 世界坐标
layout(location = 2) out vec4 o_Normal;    // 世界法线（压到 [0,1]）

void main()
{
    // albedo（与主 shader 保持一致：sRGB → Linear）
    vec4 baseColor = u_BaseColor;
    if (u_UseBaseColorMap == 1)
    {
        vec4 texColor = texture(u_BaseColorMap, v_TexCoord);
        texColor.rgb  = pow(texColor.rgb, vec3(2.2));
        baseColor *= texColor;
    }

    // 覆盖率与主画面一致；Opaque 忽略 alpha，Mask 才裁剪。
    if (u_AlphaMode == 1)
    {
        float alpha = baseColor.a;
        if (u_UseAlphaMap == 1)
            alpha *= texture(u_AlphaMap, v_TexCoord).r;
        if (alpha < u_AlphaCutoff)
            discard;
    }

    float metallic = u_Metallic;
    if (u_UseMetallicRoughnessMap == 1)
        metallic *= texture(u_MetallicRoughnessMap, v_TexCoord).b;
    vec3 diffuse = baseColor.rgb * (1.0 - metallic);

    // 方向光下每个纹素覆盖相同的投影面积。斜面的实际面积增加，
    // 恰好抵消入射余弦；这里用朝向排除背面，圆盘面积在收集时计入。
    vec3 N = normalize(v_WorldNormal);
    vec3 fluxDensity = vec3(0.0);
    if (u_NumDirectionalLights > 0 && dot(N, -u_DirLights[0].Direction.xyz) > 0.0)
        fluxDensity = diffuse * u_DirLights[0].Color.rgb * u_DirLights[0].Color.w;

    o_Flux     = vec4(fluxDensity, 1.0);
    o_WorldPos = vec4(v_WorldPos, 1.0);
    o_Normal   = vec4(N * 0.5 + 0.5, 1.0);
}
