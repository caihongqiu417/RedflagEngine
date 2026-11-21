/*
 * 从场景图提取灯光和高光。后处理把这些亮部模糊后加回原图，亮处周围就有了光晕。
 */

#type vertex
#version 450 core

layout(location = 0) in vec2 a_Position;

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

layout(std140, binding = 5) uniform BloomParams
{
    vec4 u_BloomA;  /* x 为亮度阈值。调高后，只有更亮的地方才容易出现光晕。 */
    vec4 u_BloomB;
};
#define u_Threshold (u_BloomA.x)

// 清掉异常的颜色值，免得一个坏像素在模糊时扩散到周围。
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

void main()
{
    vec3 c = SanitizeHDR(texture(u_HDR, v_TexCoord).rgb);

    // 用线性 RGB 算亮度，同样的数值，绿色比蓝色更亮。
    float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));

    // 亮度刚接近阈值时，先保留一点颜色，让光晕边缘逐渐出现。knee 是这段过渡的半宽。
    float knee = max(u_Threshold * 0.5, 0.0001);
    float soft = lum - u_Threshold + knee;
    soft = clamp(soft, 0.0, 2.0 * knee);
    soft = (soft * soft) / (4.0 * knee + 0.0001);
    float hard = max(lum - u_Threshold, 0.0);
    float contribution = max(hard, soft) / max(lum, 0.0001);

    vec3 bloom = c * contribution;

    // 把过亮的颜色压低。比如 9 压到 0.9，后面叠加几层光晕时仍能看清轮廓。
    bloom = bloom / (vec3(1.0) + bloom);

    o_Color = vec4(clamp(bloom, 0.0, 1.0), 1.0);
}
