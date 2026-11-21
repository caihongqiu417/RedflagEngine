/*
 * 从灯光看过去，记下每处最靠近灯光的表面深度。树叶的透明区域不写深度，光能从孔洞穿过。
 */

#type vertex
#version 450 core

layout(location = 0) in vec3 a_Position;
layout(location = 2) in vec2 a_TexCoord;
layout(location = 0) out vec2 v_TexCoord;

layout(std140, binding = 0) uniform Camera
{
    mat4 u_ViewProjection;
    vec4 u_ViewPosition;
    mat4 u_LightSpaceMatrix;  /* 把世界坐标投影到灯光视角。 */
    vec4 u_ShadowParams;
    vec4 u_ShadowParams2;
    mat4 u_View;
};

layout(std140, binding = 1) uniform Transform
{
    mat4 u_Transform;
};

void main()
{
    gl_Position = u_LightSpaceMatrix * u_Transform * vec4(a_Position, 1.0);
    v_TexCoord = a_TexCoord;
}

#type fragment
#version 450 core

layout(location = 0) in vec2 v_TexCoord;

// 使用和主绘制相同的材质数据，保证投影也保留贴图上的孔洞。
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
    float u_AlphaCutoff;  /* 镂空材质的最终不透明度低于这个值时，不投下阴影。 */
    int   u_AlphaMode;
    int   _matPad0;
};

layout(binding = 3) uniform sampler2D u_BaseColorMap;
layout(binding = 10) uniform sampler2D u_AlphaMap;

void main()
{
    if (u_AlphaMode == 1)
    {
        float alpha = u_BaseColor.a;
        if (u_UseBaseColorMap == 1)
            alpha *= texture(u_BaseColorMap, v_TexCoord).a;
        if (u_UseAlphaMap == 1)
            alpha *= texture(u_AlphaMap, v_TexCoord).r;
        if (alpha < u_AlphaCutoff)
            discard;
    }
}
