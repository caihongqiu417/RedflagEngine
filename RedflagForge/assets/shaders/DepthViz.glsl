/*
 * 把透视相机的深度贴图显示成灰度图，方便检查表面远近。
 * 深度纹理中的数值与实际距离不是线性关系，直接显示时大部分区域会挤在接近白色的位置。
 * 这里根据相机的近平面和远平面还原距离，再映射为灰度，让近处的变化更容易看清。
 * 这套公式用于透视相机，不能直接套到正交阴影贴图；编辑器当前另用 CPU 回读方式预览深度。
 */

// ---- 顶点阶段 ----
#type vertex
#version 450 core

layout(location = 0) in vec2 a_Position;

out vec2 v_TexCoord;

void main()
{
    v_TexCoord  = a_Position * 0.5 + 0.5;
    gl_Position = vec4(a_Position, 0.0, 1.0);
}

// ---- 片元阶段 ----
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
