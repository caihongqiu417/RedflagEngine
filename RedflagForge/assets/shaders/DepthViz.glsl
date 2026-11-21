/*
 * 把透视相机的深度画成灰度图。近处较暗，远处较亮，背景为白色。
 * 编辑器另在 CPU 上生成深度预览，这里的还原公式适用于透视投影。
 */

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
    vec4 u_NearFar;  /* x、y 分别是这台透视相机的近、远裁剪距离，单位与场景坐标相同。 */
};

void main()
{
    float d = texture(u_DepthTex, v_TexCoord).r;

    float near = u_NearFar.x;
    float far  = u_NearFar.y;

    // 透视相机存下的深度和距离并不成正比。先还原沿相机前方的距离，再从近到远画成黑到白。
    float ndc  = d * 2.0 - 1.0;
    float zLin = (2.0 * near * far) / (far + near - ndc * (far - near));
    float g    = clamp((zLin - near) / (far - near), 0.0, 1.0);

    // 提亮较暗的灰阶，让近处的深度变化更明显。
    g = pow(g, 1.0 / 2.2);

    o_Color = vec4(g, g, g, 1.0);
}
