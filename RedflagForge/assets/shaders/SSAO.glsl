/*
 * 估算表面附近有多少方向被其他物体挡住，让墙角和接触处更暗。
 * 先从深度还原表面位置，再沿表面朝向，在周围半球中放置一组测试点。
 * 把测试点投回屏幕并查询深度：若那里真实的表面更靠近相机，就记一次遮挡。
 * 最后把遮挡次数换成明亮程度：1 表示未遮挡，0 表示完全遮挡，用来减弱环境光。
 * 这里只能利用屏幕里已有的深度信息，屏幕外的物体不会参与计算。
 */

// ---- 顶点阶段 ----
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

// ---- 片元阶段 ----
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
vec3 ReconstructViewPos(vec2 uv /* 屏幕 [0,1] */, float depth01 /* OpenGL 深度 */)
{
    vec4 ndc   = vec4(uv * 2.0 - 1.0, depth01 * 2.0 - 1.0, 1.0);
    vec4 viewH = u_InvProjection * ndc;
    return viewH.xyz / viewH.w;
}

void main()
{
    // 1. 深度 → view pos
    float depth = texture(u_Depth, v_TexCoord).r;
    // 远平面或未被几何覆盖：直接输出全亮（天空）
    if (depth >= 0.9999)
    {
        o_Color = vec4(1.0);
        return;
    }

    vec3 viewPos = ReconstructViewPos(v_TexCoord, depth);

    // 2. view-space 法线
    vec3 N = texture(u_ViewNormal, v_TexCoord).xyz;
    // 法线 RT 清为 0 → 当作未覆盖像素，不产生 AO
    if (dot(N, N) < 0.01)
    {
        o_Color = vec4(1.0);
        return;
    }
    N = normalize(N);

    // 3. 随机切线（打散带状纹）
    vec3 randomVec = texture(u_Noise, v_TexCoord * u_NoiseScale.xy).xyz;
    // 从随机方向中减去沿法线的部分，得到贴着表面的切线 T；再叉乘得到另一条切线 B。
    // T、B、N 组成表面上的局部坐标轴，用它把半球中的样本转到当前表面朝向。
    vec3 T = normalize(randomVec - N * dot(randomVec, N));
    vec3 B = cross(N, T);
    mat3 TBN = mat3(T, B, N);

    float radius     = u_Params.x;
    float bias       = u_Params.y;
    int   kernelSize = int(u_Params.w);

    // 4. 半球采样累加遮蔽
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

        // 屏幕外样本不累加遮蔽，但最终仍除以 kernelSize，屏幕边缘的遮蔽会减弱。
        if (offset.x < 0.0 || offset.x > 1.0 || offset.y < 0.0 || offset.y > 1.0)
            continue;

        // 对应屏幕位置的真实表面 view-space Z
        float sampleDepth01 = texture(u_Depth, offset.xy).r;
        float sceneZ = ReconstructViewPos(offset.xy, sampleDepth01).z;

        // 屏幕上相邻的像素，实际可能相距很远；用距离减弱这类遮挡，避免物体边缘出现黑圈。
        // 相机朝向负 Z，所以 Z 越大（越接近 0），表面离相机越近。
        float rangeCheck = smoothstep(0.0, 1.0, radius / max(abs(viewPos.z - sceneZ), 0.0001));

        // 真实表面比样本点更近 → 样本被遮蔽
        occlusion += (sceneZ >= samplePos.z + bias ? 1.0 : 0.0) * rangeCheck;
    }

    // 归一化到 [0,1]，转为"可见度"，再做 power 增强对比
    occlusion = 1.0 - occlusion / float(kernelSize);
    float ao  = pow(clamp(occlusion, 0.0, 1.0), u_Params.z);

    o_Color = vec4(ao, ao, ao, 1.0);
}
