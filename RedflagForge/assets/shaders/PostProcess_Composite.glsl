/*
 * 把场景颜色和光晕相加，再调曝光。强光可以超过 1，色调映射把它压进屏幕能显示的范围。
 * 最后做 Gamma 编码，写入编辑器显示的颜色图。
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

layout(binding = 0) uniform sampler2D u_HDR;

// 光晕关闭时读到黑色，保留场景本来的颜色。
layout(binding = 1) uniform sampler2D u_Bloom;

layout(std140, binding = 5) uniform BloomParams
{
    vec4 u_BloomA;
    vec4 u_BloomB;
};
#define u_BloomIntensity (u_BloomA.y)
#define u_Exposure       (u_BloomB.z)

// 把异常的颜色分量清零，其余分量限制到 [0,64]。
vec3 SanitizeHDR(vec3 color)
{
    bvec3 invalid = bvec3((color.x != color.x) || abs(color.x) >= 1e20,
                          (color.y != color.y) || abs(color.y) >= 1e20,
                          (color.z != color.z) || abs(color.z) >= 1e20);
    color = vec3(invalid.x ? 0.0 : color.x,
                 invalid.y ? 0.0 : color.y,
                 invalid.z ? 0.0 : color.z);
    return clamp(color, vec3(0.0), vec3(64.0));
}

/*
 * 用 Narkowicz 拟合的 ACES 曲线分别压缩红、绿、蓝三个通道，保留亮部层次。
 * 曲线为 x(ax+b)/(x(cx+d)+e)。
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

vec3 SampleBloom(vec2 uv)
{
    return SanitizeHDR(texture(u_Bloom, uv).rgb);
}

void main()
{
    vec3 hdrColor = SanitizeHDR(texture(u_HDR, v_TexCoord).rgb);

    vec3 bloomColor = SampleBloom(v_TexCoord);

    // 光晕来自几层小图的叠加。这里限制加回场景的那部分亮度。
    bloomColor = clamp(bloomColor * u_BloomIntensity, 0.0, 1.0);

    vec3 combined = SanitizeHDR(hdrColor + bloomColor);

    combined *= u_Exposure;

    vec3 tonemapped = ACESTonemap(combined /* 已乘曝光的线性 RGB */);

    // 最终目标是普通 RGBA8 纹理，在这里做一次 Gamma 2.2 编码。
    vec3 gammaCorrected = pow(tonemapped, vec3(1.0 / 2.2));

    o_Color = vec4(gammaCorrected, 1.0);
}
