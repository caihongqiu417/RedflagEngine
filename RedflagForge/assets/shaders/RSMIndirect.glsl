/*
 * 估算当前表面收到的反射光，例如红墙把附近物体照出一点红色。
 * 先从屏幕深度还原表面的场景位置，再去反射阴影图中选取周围的受光点。
 * 这些点相当于小光源：距离越近、彼此越相向，给当前表面的光就越多。
 * 每帧转动采样方向，再由下一步把多帧结果混合，减轻少量采样产生的噪点。
 * 输出白色漫反射表面应有的间接亮度，主绘制再乘接收面的漫反射颜色。
 */

// ---- 顶点阶段 ----
#type vertex
#version 450 core

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

layout(location = 0) out vec4 o_Indirect;

in vec2 v_TexCoord;

// ---- Camera UBO (binding = 0) ----
layout(std140, binding = 0) uniform Camera
{
    mat4 u_ViewProjection;
    vec4 u_ViewPosition;
    mat4 u_LightSpaceMatrix;
    vec4 u_ShadowParams;
    vec4 u_ShadowParams2;
    mat4 u_View;
};

// ---- RSM Params UBO (binding = 6) ----
layout(std140, binding = 6) uniform RSMData
{
    vec4 u_RSMHeader;           // x=样本数，y=强度，z=UV 半径，w=方向光投影宽度（世界单位）
    vec4 u_RSMSamples[128];     // xy=单位圆盘偏移 z=样本权重
};

layout(binding = 13) uniform sampler2D u_RSMFlux;
layout(binding = 14) uniform sampler2D u_RSMWorldPos;
layout(binding = 15) uniform sampler2D u_RSMNormal;
layout(binding = 16) uniform sampler2D u_RSMDepth;
layout(binding = 18) uniform sampler2D u_SceneDepth;
layout(binding = 19) uniform sampler2D u_ViewNormal;

layout(std140, binding = 7) uniform RSMIndirectParams
{
    mat4 u_InvViewProj;
    int  u_FrameIndex;
    int  _rsmIPad0, _rsmIPad1, _rsmIPad2;
};


float Hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

/* 当前深度按 OpenGL 约定从 [0,1] 还原到 NDC [-1,1]，再做齐次除法。 */
vec3 ReconstructWorldPos(vec2 uv /* 屏幕 [0,1] */, float depth /* 深度附件值 */)
{
    vec4 ndc    = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 worldH = u_InvViewProj * ndc;
    return worldH.xyz / worldH.w;
}

/* 四张 RSM 附件使用同一整数纹素，避免过滤后的位置、法线与通量不对应。 */
bool SampleRSMPoint(vec2 uv /* 光源纹理坐标 */, out vec3 flux, out vec3 vplPos, out vec3 normal)
{
    ivec2 texSize = textureSize(u_RSMDepth, 0);

    ivec2 coord = clamp(ivec2(floor(uv * vec2(texSize))), ivec2(0), texSize - 1);

    float d = texelFetch(u_RSMDepth, coord, 0).r;
    if (d >= 0.9999) return false;

    flux   = texelFetch(u_RSMFlux,     coord, 0).rgb;
    vplPos = texelFetch(u_RSMWorldPos, coord, 0).rgb;
    normal = normalize(texelFetch(u_RSMNormal, coord, 0).rgb * 2.0 - 1.0);
    return true;
}

/*
 * 返回接收点的间接光权重，接收表面的 albedo 在主绘制中相乘。
 * N 是世界空间单位法线，shadowUV 是接收点在 RSM 中的投影位置。
 */
vec3 ComputeRSMIndirect(vec3 fragWorldPos, vec3 N, vec2 shadowUV)
{
    int numSamples = int(u_RSMHeader.x);
    if (numSamples <= 0) return vec3(0.0);

    float intensity    = u_RSMHeader.y;
    float sampleRadius = u_RSMHeader.z;
    float lightWidth = u_RSMHeader.w;
    float texelWidth = lightWidth / float(textureSize(u_RSMDepth, 0).x);

    // 空间 per-pixel hash + 帧间黄金比例偏移
    // 使每帧在相同像素位置旋转到不同角度，跨帧采不同 VPL 子集
    float baseAngle   = Hash12(gl_FragCoord.xy);
    float frameOffset = fract(float(u_FrameIndex) * 0.618033988749895);
    float angle = 6.28318530718 * fract(baseAngle + frameOffset);
    float cosA  = cos(angle);
    float sinA  = sin(angle);

    vec3 indirect = vec3(0.0);

    for (int i = 0; i < numSamples; ++i)
    {
        vec2  baseOffset   = u_RSMSamples[i].xy;
        float sampleWeight = u_RSMSamples[i].z;

        vec2 offset = vec2(
            cosA * baseOffset.x - sinA * baseOffset.y,
            sinA * baseOffset.x + cosA * baseOffset.y);

        vec2 vplUV = shadowUV + offset * sampleRadius;
        if (vplUV.x < 0.0 || vplUV.x > 1.0 ||
            vplUV.y < 0.0 || vplUV.y > 1.0)
            continue;

        vec3 vplFlux, vplPos, vplNormal;
        if (!SampleRSMPoint(vplUV, vplFlux, vplPos, vplNormal))
            continue;

        vec3  dir   = fragWorldPos - vplPos;
        float dist2 = dot(dir, dir);
        if (dist2 == 0.0) continue;

        vec3  dirN       = dir * inversesqrt(dist2);
        float vplFacing  = max(0.0, dot(vplNormal, dirN));
        float fragFacing = max(0.0, dot(N, -dirN));

        // 两端余弦除以距离平方。把最小距离限为一个光源纹素的宽度，
        // 避免精度误差把相邻点当作无限亮的点光源。
        float geometry = vplFacing * fragFacing / max(dist2, texelWidth * texelWidth);
        indirect += vplFlux * geometry * sampleWeight;
    }

    // 圆盘均匀采样：平均值乘世界空间投影面积。源面和接收面的
    // Lambert 漫反射各含 1/π，故 πr² / π² = r² / π。
    const float PI = 3.14159265;
    float radius = sampleRadius * lightWidth;
    return indirect * (intensity * radius * radius / (PI * float(numSamples)));
}

void main()
{
    float depth = texture(u_SceneDepth, v_TexCoord).r;
    if (depth >= 0.9999)
    {
        o_Indirect = vec4(0.0);
        return;
    }

    // 重建世界坐标
    vec3 worldPos = ReconstructWorldPos(v_TexCoord, depth);

    // view-space 法线 → 世界空间
    vec3 viewNormal = texture(u_ViewNormal, v_TexCoord).xyz;
    if (dot(viewNormal, viewNormal) < 0.01)
    {
        o_Indirect = vec4(0.0);
        return;
    }
    mat3 invView3 = transpose(mat3(u_View));
    vec3 N = normalize(invView3 * viewNormal);

    // 计算 shadow UV
    vec4 lsPos = u_LightSpaceMatrix * vec4(worldPos, 1.0);
    vec3 lsProj = lsPos.xyz / max(lsPos.w, 1e-6);
    lsProj = lsProj * 0.5 + 0.5;
    if (lsProj.z < 0.0 || lsProj.z > 1.0)
    {
        o_Indirect = vec4(0.0);
        return;
    }

    vec3 indirect = ComputeRSMIndirect(worldPos, N, lsProj.xy);
    o_Indirect = vec4(indirect, 1.0);
}
