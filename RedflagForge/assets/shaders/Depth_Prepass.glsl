/*
 * 光照之前先画一遍模型，写入相机能看到的深度和表面法线。
 * 主绘制借深度跳过被挡住的表面。SSAO 和间接光用法线判断表面朝向。
 */

#type vertex
#version 450 core

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TexCoord;

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

out vec2 v_TexCoord;
out vec3 v_ViewNormal;

void main()
{
    v_TexCoord = a_TexCoord;

    // 和主绘制一样先算世界位置，再乘相机矩阵。改变乘法顺序可能让同一点的深度略有不同。
    vec4 worldPos = u_Transform * vec4(a_Position, 1.0);
    gl_Position   = u_ViewProjection * worldPos;

    // 模型各轴缩放比例不同时，先用逆转置算出世界法线，再换到相机坐标。
    mat3 normalMatrix = mat3(transpose(inverse(u_Transform)));
    vec3 Nworld  = normalize(normalMatrix * a_Normal);
    v_ViewNormal = mat3(u_View) * Nworld;
}

#type fragment
#version 450 core

in vec2 v_TexCoord;
in vec3 v_ViewNormal;

layout(location = 2) out vec4 o_ViewNormal;  /* 相机坐标中的法线，SSAO 和间接光都会读它。 */

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
    float u_AlphaCutoff;  /* 低于这个不透明度的像素留空，让后面的模型能画出来。 */
    int   u_AlphaMode;   // 1 表示按透明度裁出轮廓。
    int   _pad0;
};

layout(binding = 3) uniform sampler2D u_BaseColorMap;
layout(binding = 10) uniform sampler2D u_AlphaMap;

void main()
{
    // 底色的不透明度、底色图的 alpha 和透明度图的红通道一起决定轮廓。
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

    // 浮点纹理可以直接存负值，读取时就能拿到原来的法线方向。
    o_ViewNormal = vec4(normalize(v_ViewNormal), 1.0);
}
