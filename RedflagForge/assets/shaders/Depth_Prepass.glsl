/*
 * 正式计算光照前，先记录相机能看到的表面深度。
 * 这样主绘制可以借助深度测试跳过被挡住的表面，减少重复的光照计算。
 * 这里同时记录表面的朝向，也就是法线，供环境遮蔽和间接光计算使用。
 * 树叶等镂空材质仍要按透明度丢弃像素，保证预先写入的深度与主绘制轮廓一致。
 * 半透明物体由调用方另行处理，不参加这次深度绘制。
 */

// ---- 顶点阶段 ----
#type vertex
#version 450 core

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;     // 输出 view-space 法线给 SSAO
layout(location = 2) in vec2 a_TexCoord;   // AlphaMap 采样需要 UV

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

    // 顺序必须和 Renderer3D_Mesh.glsl 完全一致：
    //    先算 worldPos，再乘 ViewProjection。
    //    GPU 浮点乘法不满足结合律，(A*B)*v 与 A*(B*v) 会有微小差异，
    //    导致 prepass 与主 pass 的 gl_Position.z 不一致 → Z-fighting。
    vec4 worldPos = u_Transform * vec4(a_Position, 1.0);
    gl_Position   = u_ViewProjection * worldPos;

    // 把世界法线变换到 view space：normalMatrix 用 transpose(inverse) 处理
    // 非均匀缩放，再用 mat3(u_View) 变到 view space。写入 RGBA16F 法线 RT
    // 供 SSAO Pass 重建 TBN。
    mat3 normalMatrix = mat3(transpose(inverse(u_Transform)));
    vec3 Nworld  = normalize(normalMatrix * a_Normal);
    v_ViewNormal = mat3(u_View) * Nworld;
}

// ---- 片元阶段 ----
#type fragment
#version 450 core

in vec2 v_TexCoord;
in vec3 v_ViewNormal;

// attachment 0/1 被调用方 glColorMaski 关掉了，这里只写 attachment2
layout(location = 2) out vec4 o_ViewNormal;

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

layout(binding = 3) uniform sampler2D u_BaseColorMap;
layout(binding = 10) uniform sampler2D u_AlphaMap;

void main()
{
    // ---- Mask 裁剪 ----
    // alpha = 材质因子 * BaseColorMap.a * AlphaMap.r，未启用的贴图项取 1。
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

    // ---- 输出法线 ----
    // RGBA16F 直接保存归一化的观察空间法线，不编码到 [0,1]。
    // 调用方会在 ColorMask 层面关闭 attachment0/1 的写入，不会污染颜色。
    o_ViewNormal = vec4(normalize(v_ViewNormal), 1.0);
}
