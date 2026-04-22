// =====================================================================
// ShadowDepth.glsl — 方向光 Shadow Map 深度预渲染
//
// 作用：
//   从光源视角渲染场景几何，把深度写入 Shadow Map 的 DEPTH32F 附件。
//   主 Pass 再把这张深度纹理作为比较基准，实现硬阴影 / PCF / PCSS。
//
// UBO 约定（与 Renderer3D_Mesh.glsl / Renderer3D.cpp 保持一致）：
//   binding 0 — Camera    : ViewProjection + ViewPosition +
//                           LightSpaceMatrix + ShadowParams
//   binding 1 — Transform : 逐物体模型矩阵
//
// 关键点：
//   - 只需要顶点阶段；片元阶段空转，由固定管线自动写 gl_FragDepth。
//   - 所有几何都走同一个 Transform UBO（与主 Pass 逐 draw 更新的流程相同）。
//   - 不采样任何贴图，不关心材质；Alpha Mask 的裁剪需求暂未处理
//     （后续如果要让玻璃 / 植物叶片正确投影，可把 alpha cutoff 逻辑
//      移到片元阶段，但当前场景不需要）。
// =====================================================================

// =========================== 顶点着色器 =============================
#type vertex
#version 450 core

layout(location = 0) in vec3 a_Position;   // 仅需要位置；法线 / UV 不参与深度

// ---- Camera UBO (binding = 0) ----
// 和主 Mesh shader 共享同一个绑定点；shaderc 会自动忽略本 shader
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
}

// =========================== 片元着色器 =============================
#type fragment
#version 450 core

// 无输出；深度由固定管线写入绑定的 DEPTH32F 附件
void main()
{
}
