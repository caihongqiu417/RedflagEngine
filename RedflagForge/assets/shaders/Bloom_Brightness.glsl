/*
 * 光晕的第一步：从场景图像中提取亮的部分。
 * 后续会把亮部图缩小并模糊，再加回原图，让灯光和高光向周围扩散。
 * 这里用 RGB 的加权值判断亮度。阈值附近逐渐增加贡献，避免亮部边缘突然截断。
 * 提取后的颜色还会压低，控制后续多次叠加产生的亮度。
 */

// ---- 顶点阶段 ----
#type vertex
#version 450 core

// 位置属性：xy = NDC 坐标（全屏三角 -1~3 范围覆盖整个 NDC）
layout(location = 0) in vec2 a_Position;

// 传递给片元着色器的 UV 坐标
// UV 从 [0,0] 到 [1,1]，由 NDC 位置变换而来（(pos*0.5+0.5)）
layout(location = 0) out vec2 v_TexCoord;

void main()
{
    // 将 [-1,3]×[-1,3] 的全屏三角坐标映射到 [0,2]×[0,2] UV 空间
    // 只有 [0,1]×[0,1] 部分会实际覆盖屏幕
    v_TexCoord  = a_Position * 0.5 + 0.5;
    gl_Position = vec4(a_Position, 0.0, 1.0);
}

// ---- 片元阶段 ----
#type fragment
#version 450 core

layout(location = 0) out vec4 o_Color;

layout(location = 0) in vec2 v_TexCoord;

// HDR 输入纹理（主 FBO attachment0，RGBA16F）
layout(binding = 0) uniform sampler2D u_HDR;

// CPU 把 Bloom 参数一起上传，下面的字段顺序与 PostProcess 保持一致
// u_BloomA.x = Threshold, y = Intensity, z = Radius, w = unused
// u_BloomB.xy = TexelSize, z = Exposure, w = unused
layout(std140, binding = 5) uniform BloomParams
{
    vec4 u_BloomA;
    vec4 u_BloomB;
};
#define u_Threshold (u_BloomA.x)

/* 过滤 NaN 与极大值，再限制到 [0,64]，避免异常分量扩散到整张后处理纹理。 */
vec3 SanitizeHDR(vec3 color /* 线性 RGB */)
{
    // 某些驱动在当前 shader 缓存链里对 isnan/isinf 不够稳定。
    // 这里改成：NaN 用 x != x 检出，Inf 用大值阈值拦截。
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

    // ---- 线性 RGB 加权亮度 ----
    // 人眼对绿色更敏感，因此绿色的权重最高。lum 是用来比较阈值的单个亮度值。
    float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));

    // ---- 亮度阈值附近逐渐过渡 ----
    // hard 是超过阈值的亮度；soft 在阈值前后各 knee 的范围内平滑接入。
    // 取较大值后除以原亮度，得到应保留的颜色比例，RGB 三个通道一起缩放。
    float knee = max(u_Threshold * 0.5, 0.0001);
    float soft = lum - u_Threshold + knee;
    soft = clamp(soft, 0.0, 2.0 * knee);
    soft = (soft * soft) / (4.0 * knee + 0.0001);
    float hard = max(lum - u_Threshold, 0.0);
    float contribution = max(hard, soft) / max(lum, 0.0001);

    vec3 bloom = c * contribution;

    // 用 c/(1+c) 压低过亮的颜色。例如 1 变为 0.5，9 变为 0.9，避免光晕叠加后整片发白。
    bloom = bloom / (vec3(1.0) + bloom);

    o_Color = vec4(clamp(bloom, 0.0, 1.0), 1.0);
}
