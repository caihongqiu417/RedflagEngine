// =====================================================================
// Bloom_Upsample.glsl — 泊松圆盘上采样 Pass
//
// 用途：在 Bloom 上采样链中，把更低分辨率（更大半径）的模糊结果
//       采样后以加法叠加到上一层（更高分辨率层）。
//
// 算法：泊松圆盘采样（12 点）
//   泊松分布的采样点相比均匀网格更随机，能避免方格状的规律纹理，
//   获得更自然的圆形散射效果。
//
//   所有 12 个采样点等权重（1/12），采样后求和得到上采样颜色。
//   调用方使用 GL_ONE/GL_ONE 加法 blend，本 shader 直接输出采样颜色，
//   由 OpenGL 混合单元完成与已有内容的叠加。
//
// 槽位：
//   layout(binding=0) u_Src — 当前层（更低分辨率 / 更大半径）纹理
//
// Uniforms：
//   u_TexelSize — 输入纹理的单纹素大小（= 1.0 / textureSize），
//                 用于将采样半径从"纹素数"换算到 UV 偏移
//   u_Radius    — 泊松圆盘的采样半径（纹素单位，默认 1.0）
// =====================================================================

#type vertex
#version 450 core

layout(location = 0) in  vec2 a_Position;
layout(location = 0) out vec2 v_TexCoord;

void main()
{
    v_TexCoord  = a_Position * 0.5 + 0.5;
    gl_Position = vec4(a_Position, 0.0, 1.0);
}

// ========================== 片元着色器 ==========================
#type fragment
#version 450 core

layout(location = 0) out vec4 o_Color;

layout(location = 0) in vec2 v_TexCoord;

// 输入：上采样源纹理（当前 Bloom 金字塔层，更低分辨率）
layout(binding = 0) uniform sampler2D u_Src;

// Bloom 参数 UBO（与 Bloom_Brightness / PostProcess_Composite 共享 binding=5）
// A.x=Threshold, A.y=Intensity, A.z=Radius, A.w=unused
// B.xy=TexelSize, B.zw=unused
layout(std140, binding = 5) uniform BloomParams
{
    vec4 u_BloomA;
    vec4 u_BloomB;
};
#define u_TexelSize (u_BloomB.xy)
#define u_Radius    (u_BloomA.z)

void main()
{
    // ---- 泊松圆盘采样点（12 点，单位圆内均匀分布）----
    //
    // 这 12 个向量分布在单位圆内，覆盖不同方向和距离，
    // 避免均匀网格的重复/规律性，采样出来的模糊更圆润自然。
    //
    // 坐标来源：Poisson disk with min distance ~0.37，12 points
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

    // 这一层上采样的增益。
    // 旧实现把低频结果 1:1 加回上一层，容易让能量在多级链里快速膨胀。
    // 这里给一个更保守的系数，先把“整屏发白”压住，再继续微调观感。
    const float upsampleGain = 0.35;

    // 采样偏移量 = 圆盘点坐标 × 纹素大小 × 半径
    vec2 scale = u_TexelSize * u_Radius;

    vec3 result = vec3(0.0);
    for (int i = 0; i < 12; ++i)
    {
        vec2 uv = v_TexCoord + poissonDisk[i] * scale;
        result += texture(u_Src, uv).rgb;
    }
    result *= weight * upsampleGain;

    // 直接输出采样结果；调用方使用 GL_ONE/GL_ONE 加法 blend 叠加到目标层
    o_Color = vec4(result, 1.0);
}
