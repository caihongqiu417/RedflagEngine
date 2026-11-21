/*
 * 把较小的亮部图放大，逐层加回较大的图，形成光晕。
 * 每个像素读取周围 12 个位置的颜色并取平均，亮点就会扩散到附近像素。
 * 小图上的一个像素覆盖原图更大的区域，因此小图提供较宽的光晕，大图保留细节。
 * 每层结果再乘 0.35，避免逐层相加后过亮；加法混合由 PostProcess 设置。
 */

// ---- 顶点阶段 ----
#type vertex
#version 450 core

layout(location = 0) in  vec2 a_Position;
layout(location = 0) out vec2 v_TexCoord;

void main()
{
    v_TexCoord  = a_Position * 0.5 + 0.5;
    gl_Position = vec4(a_Position, 0.0, 1.0);
}

// ---- 片元阶段 ----
#type fragment
#version 450 core

layout(location = 0) out vec4 o_Color;

layout(location = 0) in vec2 v_TexCoord;

// 输入：上采样源纹理（当前 Bloom 金字塔层，更低分辨率）
layout(binding = 0) uniform sampler2D u_Src;

// Bloom 参数 UBO（与 Bloom_Brightness / PostProcess_Composite 共享 binding=5）
// A.x=Threshold, A.y=Intensity, A.z=Radius, A.w=unused
// B.xy=TexelSize, B.z=Exposure（此阶段未用）, B.w=unused
layout(std140, binding = 5) uniform BloomParams
{
    vec4 u_BloomA;
    vec4 u_BloomB;
};
#define u_TexelSize (u_BloomB.xy)
#define u_Radius    (u_BloomA.z)

void main()
{
    // ---- 固定圆盘采样核 ----
    // 12 个偏移覆盖不同方向和距离；每个目标像素使用相同采样核。
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

    // 每个采样点的等权重（等权重平均）
    const float weight = 1.0 / 12.0;

    // 每层乘 0.35，控制多层加法叠加的增益。
    const float upsampleGain = 0.35;

    // ---- 找到周围的采样位置 ----
    // UV 从 0 到 1 覆盖整张纹理，所以一个纹理像素对应的偏移是 1/纹理尺寸。
    // 把以像素计的半径换成 UV 偏移，再用圆盘中的方向分散到周围 12 个位置。
    vec2 scale = u_TexelSize * u_Radius;

    vec3 result = vec3(0.0);
    for (int i = 0; i < 12; ++i)
    {
        vec2 uv = v_TexCoord + poissonDisk[i] * scale;
        result += texture(u_Src, uv).rgb;
    }
    result *= weight * upsampleGain;

    // 直接输出采样结果，调用方使用 GL_ONE/GL_ONE 加法 blend 叠加到目标层
    o_Color = vec4(result, 1.0);
}
