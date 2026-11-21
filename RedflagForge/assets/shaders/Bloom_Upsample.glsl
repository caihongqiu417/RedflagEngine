/*
 * 将小图上模糊的亮部加回上一层大图。小图上的一个像素能覆盖原图的一片区域，
 * 逐层放大后，亮点周围就有了宽窄不同的光晕。
 */

#type vertex
#version 450 core

layout(location = 0) in  vec2 a_Position;
layout(location = 0) out vec2 v_TexCoord;

void main()
{
    v_TexCoord  = a_Position * 0.5 + 0.5;
    gl_Position = vec4(a_Position, 0.0, 1.0);
}

#type fragment
#version 450 core

layout(location = 0) out vec4 o_Color;

layout(location = 0) in vec2 v_TexCoord;

// 上一轮画好的亮部小图。
layout(binding = 0) uniform sampler2D u_Src;

layout(std140, binding = 5) uniform BloomParams
{
    vec4 u_BloomA;  /* z 为模糊半径，单位是输入这张小图的像素。 */
    vec4 u_BloomB;  /* xy 表示沿横、纵方向移动一个像素时 UV 改变多少。 */
};
#define u_TexelSize (u_BloomB.xy)
#define u_Radius    (u_BloomA.z)

void main()
{
    // 同时取亮点周围十二处的颜色，避免模糊只沿着横竖两轴。
    const vec2 poissonDisk[12] = vec2[12](
        vec2(-0.326,  0.406),
        vec2(-0.840, -0.074),
        vec2(-0.696,  0.457),
        vec2(-0.203, -0.621),
        vec2( 0.962, -0.195),
        vec2( 0.473, -0.480),
        vec2( 0.519,  0.767),
        vec2( 0.185, -0.893),
        vec2( 0.507,  0.064),
        vec2( 0.896,  0.412),
        vec2(-0.322, -0.933),
        vec2(-0.792, -0.598)
    );

    const float weight = 1.0 / 12.0;

    // 每层只加回一部分颜色，防止多层叠加把灯光周围变成一片白。
    const float upsampleGain = 0.35;

    vec2 scale = u_TexelSize * u_Radius;

    vec3 result = vec3(0.0);
    for (int i = 0; i < 12; ++i)
    {
        vec2 uv = v_TexCoord + poissonDisk[i] * scale;
        result += texture(u_Src, uv).rgb;
    }
    result *= weight * upsampleGain;

    o_Color = vec4(result, 1.0);
}
