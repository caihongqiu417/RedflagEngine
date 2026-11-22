/*
 * 估算物体靠近墙角或地面时失去的环境光。把测试点放在表面朝外的半球内，再投回屏幕检查深度。
 * 如果相机先看到了更近的表面，就记下一次遮挡。最后输出 0 到 1，越暗表示被挡住的光越多。
 */

#type vertex
#version 450 core

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
    vec4 u_Params;        // x、y 分别是采样半径和判断遮挡所需的深度差，都用场景单位。z 调整阴影深浅，w 是样本数。
    vec4 u_NoiseScale;    // 按视口大小平铺 4×4 噪声图，让相邻像素采用不同的采样方向。
    vec4 u_Kernel[64];
};

layout(binding = 13) uniform sampler2D u_Depth;
layout(binding = 14) uniform sampler2D u_ViewNormal;
layout(binding = 15) uniform sampler2D u_Noise;

vec3 ReconstructViewPos(vec2 uv, float depth01)
{
    vec4 ndc   = vec4(uv * 2.0 - 1.0, depth01 * 2.0 - 1.0, 1.0);
    vec4 viewH = u_InvProjection * ndc;
    return viewH.xyz / viewH.w;
}

void main()
{
    float depth = texture(u_Depth, v_TexCoord).r;
    // 天空和远裁剪面附近不加阴影。
    if (depth >= 0.9999)
    {
        o_Color = vec4(1.0);
        return;
    }

    vec3 viewPos = ReconstructViewPos(v_TexCoord, depth);

    vec3 N = texture(u_ViewNormal, v_TexCoord).xyz;
    // 没画模型的位置，法线图里是零。
    if (dot(N, N) < 0.01)
    {
        o_Color = vec4(1.0);
        return;
    }
    N = normalize(N);

    vec3 randomVec = texture(u_Noise, v_TexCoord * u_NoiseScale.xy).xyz;
    // 先从随机方向中去掉沿法线的部分，得到贴着表面的切线 T，再用 T 和 N 搭出半球的方向。
    vec3 T = normalize(randomVec - N * dot(randomVec, N));
    vec3 B = cross(N, T);
    mat3 TBN = mat3(T, B, N);

    float radius     = u_Params.x;  /* 以场景单位计，调大后能算到更远处的遮挡。 */
    float bias       = u_Params.y;  /* 遮挡面至少要比测试点靠近这么多，减轻平面上的黑斑。 */
    int   kernelSize = int(u_Params.w);

    float occlusion = 0.0;
    for (int i = 0; i < kernelSize; ++i)
    {
        // 让半球朝向当前表面的法线，再把测试点放到表面附近。
        vec3 samplePos = TBN * u_Kernel[i].xyz;
        samplePos = viewPos + samplePos * radius;

        // 从相机看这个测试点，找出它落在哪个屏幕像素。
        vec4 offset = u_Projection * vec4(samplePos, 1.0);
        offset.xyz /= offset.w;
        offset.xy  = offset.xy * 0.5 + 0.5;

        // 落在屏幕外的测试点按没有遮挡计算，所以画面边缘的阴影可能偏浅。
        if (offset.x < 0.0 || offset.x > 1.0 || offset.y < 0.0 || offset.y > 1.0)
            continue;

        float sampleDepth01 = texture(u_Depth, offset.xy).r;
        float sceneZ = ReconstructViewPos(offset.xy /* 测试点所在的屏幕位置 */, sampleDepth01).z;

        // 物体轮廓旁的像素可能落在远处的墙上。两者深度相差超过采样半径时，让这个点少算一些遮挡，以免轮廓多出一圈暗边。
        // 相机朝向负 Z，sceneZ 越大代表看到的表面越近。
        float rangeCheck = smoothstep(0.0, 1.0, radius / max(abs(viewPos.z - sceneZ), 0.0001));

        occlusion += (sceneZ >= samplePos.z + bias ? 1.0 : 0.0) * rangeCheck;
    }

    // 先算有多少方向还透得过光，再用幂指数调整遮挡处的明暗。
    occlusion = 1.0 - occlusion / float(kernelSize);
    float ao  = pow(clamp(occlusion, 0.0, 1.0), u_Params.z);

    o_Color = vec4(ao, ao, ao, 1.0);
}
