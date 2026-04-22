// =====================================================================
// SSAO.glsl — Screen-Space Ambient Occlusion（无滤波，采样更密）
//
// 输入：
//   slot 13 — 主 FBO 深度（sampler2D，直接采 .r）
//   slot 14 — 主 FBO view-space 法线（RGBA16F，Depth_Prepass 写入）
//   slot 15 — 4×4 随机切线纹理（GL_REPEAT）
//
// UBO (binding = 4, std140)：
//   mat4 u_Projection
//   mat4 u_InvProjection
//   vec4 u_Params       : x=radius, y=bias, z=power, w=kernelSize
//   vec4 u_NoiseScale   : xy = viewportSize/4
//   vec4 u_Kernel[64]   : .xyz 半球样本，w 未用
//
// 输出：
//   o_Color.r = AO（1=完全亮，0=完全遮蔽）
//
// 说明：
//   为了"简单"放弃模糊 Pass，改为在一次 SSAO 运算里取更多样本（默认 64）。
//   输出会在主 Mesh shader 里直接乘到 ambient，有轻微噪点但可接受。
// =====================================================================

#type vertex
#version 450 core

// 全屏三角形 trick：一个覆盖整个屏幕的大三角
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

layout(std140, binding = 4) uniform SSAOParams
{
    mat4 u_Projection;
    mat4 u_InvProjection;
    vec4 u_Params;        // x=radius, y=bias, z=power, w=kernelSize
    vec4 u_NoiseScale;    // xy = viewportSize/4
    vec4 u_Kernel[64];
};

layout(binding = 13) uniform sampler2D u_Depth;
layout(binding = 14) uniform sampler2D u_ViewNormal;
layout(binding = 15) uniform sampler2D u_Noise;

// 从窗口 UV + [0,1] depth 反投影回 view space
vec3 ReconstructViewPos(vec2 uv, float depth01)
{
    vec4 ndc   = vec4(uv * 2.0 - 1.0, depth01 * 2.0 - 1.0, 1.0);
    vec4 viewH = u_InvProjection * ndc;
    return viewH.xyz / viewH.w;
}

void main()
{
    // --- 1. 深度 → view pos ---
    float depth = texture(u_Depth, v_TexCoord).r;
    // 远平面或未被几何覆盖：直接输出全亮（天空）
    if (depth >= 0.9999)
    {
        o_Color = vec4(1.0);
        return;
    }

    vec3 viewPos = ReconstructViewPos(v_TexCoord, depth);

    // --- 2. view-space 法线 ---
    vec3 N = texture(u_ViewNormal, v_TexCoord).xyz;
    // 法线 RT 清为 0 → 当作未覆盖像素，不产生 AO
    if (dot(N, N) < 0.01)
    {
        o_Color = vec4(1.0);
        return;
    }
    N = normalize(N);

    // --- 3. 随机切线（打散带状纹） ---
    vec3 randomVec = texture(u_Noise, v_TexCoord * u_NoiseScale.xy).xyz;
    // Gram-Schmidt 构造 TBN（N 为 Z 轴），把半球样本变到 view space
    vec3 T = normalize(randomVec - N * dot(randomVec, N));
    vec3 B = cross(N, T);
    mat3 TBN = mat3(T, B, N);

    float radius     = u_Params.x;
    float bias       = u_Params.y;
    int   kernelSize = int(u_Params.w);

    // --- 4. 半球采样累加遮蔽 ---
    float occlusion = 0.0;
    for (int i = 0; i < kernelSize; ++i)
    {
        // 样本点从切线空间转到 view space，再按 radius 偏移到表面周围
        vec3 samplePos = TBN * u_Kernel[i].xyz;
        samplePos = viewPos + samplePos * radius;

        // 投回裁剪空间 → UV
        vec4 offset = u_Projection * vec4(samplePos, 1.0);
        offset.xyz /= offset.w;
        offset.xy  = offset.xy * 0.5 + 0.5;

        // 屏幕外样本跳过（不贡献遮蔽，也不算"可见"）
        if (offset.x < 0.0 || offset.x > 1.0 || offset.y < 0.0 || offset.y > 1.0)
            continue;

        // 对应屏幕位置的真实表面 view-space Z
        float sampleDepth01 = texture(u_Depth, offset.xy).r;
        float sceneZ = ReconstructViewPos(offset.xy, sampleDepth01).z;

        // 深度超出半径范围就不算（防止远处几何产生 halo）
        // view space 中"靠近相机" = Z 更大（负值更接近 0）
        float rangeCheck = smoothstep(0.0, 1.0, radius / max(abs(viewPos.z - sceneZ), 0.0001));

        // 真实表面比样本点更近 → 样本被遮蔽
        occlusion += (sceneZ >= samplePos.z + bias ? 1.0 : 0.0) * rangeCheck;
    }

    // 归一化到 [0,1]，转为"可见度"，再做 power 增强对比
    occlusion = 1.0 - occlusion / float(kernelSize);
    float ao  = pow(clamp(occlusion, 0.0, 1.0), u_Params.z);

    o_Color = vec4(ao, ao, ao, 1.0);
}
