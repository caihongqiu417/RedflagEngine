// =====================================================================
// Depth_Prepass.glsl — 主相机深度预传递 (Z-Prepass)
//
// 作用：
//   在正式彩色渲染之前，用主相机视角把全部不透明/Mask几何的深度
//   写入主帧缓冲的深度缓冲。正式着色 Pass 启用的 Early-Z 可以据此
//   在片元着色器运行之前直接跳过被遮挡的像素，减少 PBR 着色浪费。
//
// UBO 约定（与 Renderer3D_Mesh.glsl 完全一致）：
//   binding 0 — Camera    : u_ViewProjection（主相机）
//   binding 1 — Transform : 逐物体模型矩阵
//   binding 2 — Material  : PBR 参数（此处只用 AlphaCutoff / AlphaMode / UseAlphaMap）
//
// 纹理槽位：
//   slot 10 — AlphaMap（AlphaMode=Mask 时采样，其余不用）
//
// 注意：
//   - 片元着色器不写任何颜色（调用方已用 glColorMask 关闭颜色写入）
//   - Opaque / Mask 材质正常写深度；Blend 材质由调用方过滤，不进入此 Pass
// =====================================================================

// =========================== 顶点着色器 =============================
#type vertex
#version 450 core

layout(location = 0) in vec3 a_Position;
layout(location = 2) in vec2 a_TexCoord;   // AlphaMap 采样需要 UV

layout(std140, binding = 0) uniform Camera
{
    mat4 u_ViewProjection;
    vec4 u_ViewPosition;
    mat4 u_LightSpaceMatrix;
    vec4 u_ShadowParams;
    vec4 u_ShadowParams2;
};

layout(std140, binding = 1) uniform Transform
{
    mat4 u_Transform;
};

out vec2 v_TexCoord;

void main()
{
    v_TexCoord = a_TexCoord;

    // ⚠️ 顺序必须和 Renderer3D_Mesh.glsl 完全一致：
    //    先算 worldPos，再乘 ViewProjection。
    //    GPU 浮点乘法不满足结合律，(A*B)*v 与 A*(B*v) 会有微小差异，
    //    导致 prepass 与主 pass 的 gl_Position.z 不一致 → Z-fighting。
    vec4 worldPos = u_Transform * vec4(a_Position, 1.0);
    gl_Position   = u_ViewProjection * worldPos;
}

// =========================== 片元着色器 =============================
#type fragment
#version 450 core

in vec2 v_TexCoord;

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
    int   u_AlphaMode;   // 0=Opaque, 1=Mask, 2=Blend
    int   _pad0;
};

uniform sampler2D u_AlphaMap;   // slot 10

void main()
{
    // AlphaMode == 1 (Mask)：根据 alpha 贴图 + cutoff 裁剪，保持与主 Pass 一致
    if (u_AlphaMode == 1)
    {
        float alpha = u_UseAlphaMap == 1
            ? texture(u_AlphaMap, v_TexCoord).r
            : u_BaseColor.a;
        if (alpha < u_AlphaCutoff)
            discard;
    }
    // 不输出颜色；调用方已关闭 glColorMask，片元深度由固定管线写入
}
