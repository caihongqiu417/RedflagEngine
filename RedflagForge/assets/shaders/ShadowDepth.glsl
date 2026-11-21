/*
 * 从灯光方向看场景，记录光线最先遇到的表面深度，得到阴影贴图。
 * 正式绘制时，也把当前表面换算到灯光视角：若它比贴图记录的深度更远，就被前面的表面挡住。
 * 本次只写深度，不计算颜色。相机矩阵改用灯光的矩阵，顶点仍先经过模型变换。
 * 镂空表面沿用主画面的透明度裁剪，只有可见部分写入阴影深度。
 */

// ---- 顶点阶段 ----
#type vertex
#version 450 core

layout(location = 0) in vec3 a_Position;
layout(location = 2) in vec2 a_TexCoord;
layout(location = 0) out vec2 v_TexCoord;

// ---- Camera UBO (binding = 0) ----
// 和主 Mesh shader 共享同一个绑定点。shaderc 会自动忽略本 shader
// 没有使用到的字段。我们只需要 u_LightSpaceMatrix。
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

void main()
{
    // 把顶点变换到光源裁剪空间：世界 → 光源 View → 光源 Ortho
    gl_Position = u_LightSpaceMatrix * u_Transform * vec4(a_Position, 1.0);
    v_TexCoord = a_TexCoord;
}

// ---- 片元阶段 ----
#type fragment
#version 450 core

layout(location = 0) in vec2 v_TexCoord;

// 字段顺序与 C++ 材质数据一致，共 64 字节；这里仅使用透明度相关字段。
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

layout(binding = 3) uniform sampler2D u_BaseColorMap;
layout(binding = 10) uniform sampler2D u_AlphaMap;

// 无颜色输出；通过覆盖率测试后，固定管线写入深度附件。
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
