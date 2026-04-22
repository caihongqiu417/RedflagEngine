// =====================================================================
// DepthViz.glsl — 深度可视化
//
// 把非线性深度纹理线性化后输出为灰度颜色，用于 ImGui 调试面板预览。
//
// 输入：
//   slot 0 — 深度纹理（sampler2D，.r = [0,1] 非线性深度）
//   UBO binding=5 — vec4(near, far, 0, 0)
//
// 输出：
//   o_Color — 线性化 + gamma 的灰度颜色
// =====================================================================

#type vertex
#version 450 core

layout(location = 0) in vec2 a_Position;

out vec2 v_TexCoord;

void main()
{
    v_TexCoord  = a_Position * 0.5 + 0.5;
    gl_Position = vec4(a_Position, 0.0, 1.0);
}

#type fragment
#version 450 core

layout(location = 0) out vec4 o_Color;

in vec2 v_TexCoord;

layout(binding = 0) uniform sampler2D u_DepthTex;

layout(std140, binding = 5) uniform DepthVizParams
{
    vec4 u_NearFar; // x = near, y = far
};

void main()
{
    float d = texture(u_DepthTex, v_TexCoord).r;

    float near = u_NearFar.x;
    float far  = u_NearFar.y;

    // 透视深度线性化
    float ndc  = d * 2.0 - 1.0;
    float zLin = (2.0 * near * far) / (far + near - ndc * (far - near));
    float g    = clamp((zLin - near) / (far - near), 0.0, 1.0);

    // gamma 矫正让近处对比更明显
    g = pow(g, 1.0 / 2.2);

    o_Color = vec4(g, g, g, 1.0);
}
