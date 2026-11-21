/*
 * 把光晕与场景颜色合成，转换为屏幕显示的图像。
 * 场景颜色使用 HDR 保存，数值可以大于 1，这样强光的信息不会提前丢失。
 * 先把模糊后的亮部图加回场景，再乘曝光值调整整体明暗。
 * 随后用色调映射压缩强光，把颜色放进显示范围，最后做 Gamma 编码供屏幕显示。
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

// HDR 原图（主 FBO attachment0，RGBA16F）
layout(binding = 0) uniform sampler2D u_HDR;

// Bloom 叠加图（Bloom 金字塔第 0 层）
// Bloom 关闭时 PostProcess 绑定 1x1 黑纹理，叠加项为零。
layout(binding = 1) uniform sampler2D u_Bloom;

// CPU 把 Bloom 参数一起上传，下面的字段顺序与 PostProcess 保持一致
// u_BloomA.x = Threshold, y = Intensity, z = Radius, w = unused
// u_BloomB.xy = TexelSize, z = Exposure, w = unused
layout(std140, binding = 5) uniform BloomParams
{
    vec4 u_BloomA;
    vec4 u_BloomB;
};
#define u_BloomIntensity (u_BloomA.y)
#define u_Exposure       (u_BloomB.z)

/* 过滤 NaN 与极大值，再限制到 [0,64]，避免异常分量扩散到整张后处理纹理。 */
vec3 SanitizeHDR(vec3 color /* 线性 RGB */)
{
    // 避开 isnan/isinf 在当前 shaderc -> SPIR-V -> OpenGL 链上的兼容性风险。
    // NaN 满足 x != x，Inf 会满足 abs(x) >= 1e20。两者都归零，正常值再做范围钳制。
    bvec3 invalid = bvec3((color.x != color.x) || abs(color.x) >= 1e20,
                          (color.y != color.y) || abs(color.y) >= 1e20,
                          (color.z != color.z) || abs(color.z) >= 1e20);
    color = vec3(invalid.x ? 0.0 : color.x,
                 invalid.y ? 0.0 : color.y,
                 invalid.z ? 0.0 : color.z);
    return clamp(color, vec3(0.0), vec3(64.0));
}

/*
 * 色调映射把很大的亮度压进 [0,1]，让强光保留层次，而不是全部截成白色。
 * x 是乘过曝光的颜色；这里用 Narkowicz 拟合曲线 x(ax+b)/(x(cx+d)+e) 逐通道计算。
 * 来源：https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
 */
vec3 ACESTonemap(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// PostProcess 已调度 GPU 上采样，把各层合并到第 0 层，此处直接读取结果。
vec3 SampleBloom(vec2 uv)
{
    return SanitizeHDR(texture(u_Bloom, uv).rgb);
}

void main()
{
    // 1. 采样 HDR 原图
    vec3 hdrColor = SanitizeHDR(texture(u_HDR, v_TexCoord).rgb);

    // 2. 直接采样已完成上采样叠加的 Bloom mip[0]
    vec3 bloomColor = SampleBloom(v_TexCoord);

    // Bloom 图本身是 [0,1]，这里再次限制一下最终进入合成的强度范围。
    bloomColor = clamp(bloomColor * u_BloomIntensity, 0.0, 1.0);

    // 3. HDR 叠加：直接在线性空间相加
    vec3 combined = SanitizeHDR(hdrColor + bloomColor);

    // 3.5 曝光度：在 tonemap 之前乘以曝光系数（值越大画面越亮）
    combined *= u_Exposure;

    // 4. ACES 色调映射：HDR [0, ∞) → LDR [0, 1]
    vec3 tonemapped = ACESTonemap(combined);

    // 5. 用幂函数近似 Gamma 2.2 编码，输出到普通 RGBA8 目标。
    vec3 gammaCorrected = pow(tonemapped, vec3(1.0 / 2.2));

    o_Color = vec4(gammaCorrected, 1.0);
}
