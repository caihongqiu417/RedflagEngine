/*
 * 从方向光看场景，记录它照到的表面位置、法线和反射光颜色。
 * 例如红墙受光的部分会成为红色的间接光源，树叶贴图中的孔洞则不参与反射。
 */

#type vertex
#version 450 core

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TexCoord;

// 与主画面共用相机参数，这次用光源矩阵决定顶点画在哪里。
layout(std140, binding = 0) uniform Camera
{
    mat4 u_ViewProjection;
    vec4 u_ViewPosition;
    mat4 u_LightSpaceMatrix;
    vec4 u_ShadowParams;
    vec4 u_ShadowParams2;
    mat4 u_View;
};

layout(std140, binding = 1) uniform Transform
{
    mat4 u_Transform;
};

layout(location = 0) out vec3 v_WorldPos;
layout(location = 1) out vec3 v_WorldNormal;
layout(location = 2) out vec2 v_TexCoord;

void main()
{
    vec4 worldPos = u_Transform * vec4(a_Position, 1.0);
    v_WorldPos    = worldPos.xyz;

    // 模型沿各轴缩放的比例可能不同，逆转置能让法线仍然垂直于表面。
    mat3 normalMatrix = mat3(transpose(inverse(u_Transform)));
    v_WorldNormal     = normalize(normalMatrix * a_Normal);

    v_TexCoord    = a_TexCoord;
    gl_Position   = u_LightSpaceMatrix * worldPos;
}

#type fragment
#version 450 core

layout(location = 0) in vec3 v_WorldPos;
layout(location = 1) in vec3 v_WorldNormal;
layout(location = 2) in vec2 v_TexCoord;

// Renderer3D 同时把底色、金属度和透明度参数写在这组数据里。
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

layout(binding = 3) uniform sampler2D u_BaseColorMap;
layout(binding = 5) uniform sampler2D u_MetallicRoughnessMap;
layout(binding = 10) uniform sampler2D u_AlphaMap;

layout(location = 0) out vec4 o_Flux;      /* 每单位光源投影面积的反射通量，RGB 保留反射光颜色。 */
layout(location = 1) out vec4 o_WorldPos;  // 世界坐标
layout(location = 2) out vec4 o_Normal;    // 世界法线（压到 [0,1]）

void main()
{
    // 底色贴图用 sRGB 存储，先转成线性颜色再计算反射光。
    vec4 baseColor = u_BaseColor;
    if (u_UseBaseColorMap == 1)
    {
        vec4 texColor = texture(u_BaseColorMap, v_TexCoord);
        texColor.rgb  = pow(texColor.rgb, vec3(2.2));
        baseColor *= texColor;
    }

    // 叶片等镂空材质只让实际留下的部分反光。
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
