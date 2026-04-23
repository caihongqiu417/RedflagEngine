// =====================================================================
// PostProcess_Composite.glsl — 后处理合成 Pass
//
// 用途：将 HDR 渲染结果与 Bloom 效果合并，执行色调映射和 Gamma 校正，
//       输出最终 LDR 颜色到 s_PostFBO（RGBA8），供 ForgeLayer 显示。
//
// 流程：
//   1. 采样 HDR 原图（主 FBO attachment0）
//   2. 采样 Bloom 结果（Bloom 金字塔第 0 层，多频段叠加）
//   3. 叠加：finalHDR = hdrColor + bloomColor * u_BloomIntensity
//   4. ACES 色调映射（Hill 近似）：HDR [0,∞) → LDR [0,1]
//   5. Gamma 2.2 校正：线性空间 → sRGB 显示空间
//
// 槽位：
//   binding = 0  u_HDR    — 主 FBO attachment0（RGBA16F HDR）
//   binding = 1  u_Bloom  — Bloom 第 0 层（RGBA16F，多频段叠加结果）
//
// Uniforms：
//   u_BloomIntensity — Bloom 叠加强度（默认 1.0，ImGui 可调）
//
// 关于 ACES：
//   ACES（Academy Color Encoding System）是电影行业标准的色调曲线。
//   相比 Reinhard（color/(color+1)），ACES 能更好地保留中间调对比度，
//   同时对高光区域有更自然的压缩效果（肩部更宽，颜色不会死白）。
//   这里使用 Stephen Hill 的多项式近似（5 次方），无需 LUT 贴图。
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

// HDR 原图（主 FBO attachment0，RGBA16F）
layout(binding = 0) uniform sampler2D u_HDR;

// Bloom 叠加图（Bloom 金字塔第 0 层）
// 若 Bloom 关闭，CPU 端绑 GL 纹理 0（采样返回黑色），加法无效果
layout(binding = 1) uniform sampler2D u_Bloom;

// Bloom 参数 UBO（shaderc/Vulkan 不允许块外非 opaque uniform）
// u_BloomA.x = Threshold, y = Intensity, z = Radius, w = unused
// u_BloomB.xy = TexelSize,           zw = unused
layout(std140, binding = 5) uniform BloomParams
{
    vec4 u_BloomA;
    vec4 u_BloomB;
};
#define u_BloomIntensity (u_BloomA.y)
#define u_BloomRadius    (u_BloomA.z)
#define u_BloomTexelSize (u_BloomB.xy)
#define u_Exposure       (u_BloomB.z)

vec3 SanitizeHDR(vec3 color)
{
    // 避开 isnan/isinf 在当前 shaderc -> SPIR-V -> OpenGL 链上的兼容性风险。
    // NaN 满足 x != x，Inf 会满足 abs(x) >= 1e20；两者都归零，正常值再做范围钳制。
    bvec3 invalid = bvec3((color.x != color.x) || abs(color.x) >= 1e20,
                          (color.y != color.y) || abs(color.y) >= 1e20,
                          (color.z != color.z) || abs(color.z) >= 1e20);
    color = vec3(invalid.x ? 0.0 : color.x,
                 invalid.y ? 0.0 : color.y,
                 invalid.z ? 0.0 : color.z);
    return clamp(color, vec3(0.0), vec3(64.0));
}

// ---- ACES Filmic 近似（Narkowicz）----
// 相比矩阵版 Hill 近似，这个版本更短、更稳，也更适合先把显示链路跑通。
vec3 ACESTonemap(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 SampleBloomBlur(vec2 uv)
{
    vec2 offset = u_BloomTexelSize * max(u_BloomRadius, 0.1);

    // 稳定的 9-tap 小核：中心 + 十字 + 对角。
    // 这里不再依赖外部金字塔上采样，直接在 composite 里完成柔化。
    // 权重总和严格归一化到 1，避免模糊本身放大能量。
    const float wCenter = 0.20;
    const float wCross  = 0.12;
    const float wDiag   = 0.08;

    vec3 sum = vec3(0.0);
    sum += texture(u_Bloom, uv).rgb * wCenter;
    sum += texture(u_Bloom, uv + vec2( offset.x,  0.0)).rgb * wCross;
    sum += texture(u_Bloom, uv + vec2(-offset.x,  0.0)).rgb * wCross;
    sum += texture(u_Bloom, uv + vec2( 0.0,  offset.y)).rgb * wCross;
    sum += texture(u_Bloom, uv + vec2( 0.0, -offset.y)).rgb * wCross;
    sum += texture(u_Bloom, uv + vec2( offset.x,  offset.y)).rgb * wDiag;
    sum += texture(u_Bloom, uv + vec2(-offset.x,  offset.y)).rgb * wDiag;
    sum += texture(u_Bloom, uv + vec2( offset.x, -offset.y)).rgb * wDiag;
    sum += texture(u_Bloom, uv + vec2(-offset.x, -offset.y)).rgb * wDiag;
    return SanitizeHDR(sum);
}

void main()
{
    // 1. 采样 HDR 原图
    vec3 hdrColor = SanitizeHDR(texture(u_HDR, v_TexCoord).rgb);

    // 2. 对半分辨率 Bloom 图做一个小范围模糊，再叠加强度
    vec3 bloomColor = SampleBloomBlur(v_TexCoord);

    // Bloom 图本身是 [0,1]，这里再次限制一下最终进入合成的强度范围。
    bloomColor = clamp(bloomColor * u_BloomIntensity, 0.0, 1.0);

    // 3. HDR 叠加：直接在线性空间相加
    vec3 combined = SanitizeHDR(hdrColor + bloomColor);

    // 3.5 曝光度：在 tonemap 之前乘以曝光系数（值越大画面越亮）
    combined *= u_Exposure;

    // 4. ACES 色调映射：HDR [0, ∞) → LDR [0, 1]
    vec3 tonemapped = ACESTonemap(combined);

    // 5. Gamma 2.2 校正：线性空间 → sRGB 显示空间
    vec3 gammaCorrected = pow(tonemapped, vec3(1.0 / 2.2));

    o_Color = vec4(gammaCorrected, 1.0);
}
