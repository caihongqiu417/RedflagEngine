// =====================================================================
// Bloom_Brightness.glsl — 亮度提取 Pass
//
// 用途：从 HDR 渲染结果中提取亮度超过阈值的片元，
//       作为 Bloom 效果的输入（下采样金字塔的第 0 层）。
//
// 算法：
//   1. 采样 HDR 纹理
//   2. 计算感知亮度（Luminance）：加权 RGB，符合人眼对各通道敏感度
//   3. 软截断：brightness = c * max(0, lum - threshold) / max(lum, ε)
//      效果：亮度恰好等于 threshold 时输出为零，超出越多保留越多，平滑过渡
//
// 槽位：
//   layout(binding=0) u_HDR — 主 FBO attachment0（RGBA16F HDR）
//
// Uniforms：
//   u_Threshold — 亮度阈值，ImGui 可调（默认 1.0）
// =====================================================================

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

// ========================== 片元着色器 ==========================
#type fragment
#version 450 core

layout(location = 0) out vec4 o_Color;

layout(location = 0) in vec2 v_TexCoord;

// HDR 输入纹理（主 FBO attachment0，RGBA16F）
layout(binding = 0) uniform sampler2D u_HDR;

// Bloom 参数 UBO（shaderc/Vulkan 不允许块外非 opaque uniform）
// u_BloomA.x = Threshold, y = Intensity, z = Radius, w = unused
// u_BloomB.xy = TexelSize,           zw = unused
layout(std140, binding = 5) uniform BloomParams
{
    vec4 u_BloomA;
    vec4 u_BloomB;
};
#define u_Threshold (u_BloomA.x)

vec3 SanitizeHDR(vec3 color)
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

    // ---- 感知亮度（ITU-R BT.601）----
    // 绿色权重最高（人眼最敏感），蓝色最低
    float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));

    // ---- 软阈值 + Knee ----
    // 直接用 max(lum-threshold, 0) 在 HDR 场景里容易保留过多能量，
    // 导致稍微亮一些的大片区域也被当作 Bloom 光源，最后整屏发白。
    // 这里使用 soft knee，让阈值附近平滑进入 Bloom。
    float knee = max(u_Threshold * 0.5, 0.0001);
    float soft = lum - u_Threshold + knee;
    soft = clamp(soft, 0.0, 2.0 * knee);
    soft = (soft * soft) / (4.0 * knee + 0.0001);
    float hard = max(lum - u_Threshold, 0.0);
    float contribution = max(hard, soft) / max(lum, 0.0001);

    vec3 bloom = c * contribution;

    // 再做一次能量压缩，把提取结果压进 [0,1)。
    // 这样 Bloom 图本身就不会携带过高 HDR 值，后面即使模糊叠加也不容易把整屏冲白。
    bloom = bloom / (vec3(1.0) + bloom);

    o_Color = vec4(clamp(bloom, 0.0, 1.0), 1.0);
}
