// =====================================================================
// RSMIndirect.glsl — 屏幕空间 RSM 间接光延迟 Pass
//
// 从 Z-Prepass 产出的深度和 view-space 法线重建世界坐标/法线，
// 计算 RSM 间接光（同 Renderer3D_Mesh.glsl 的物理公式），
// 但旋转角加入帧号驱动的 jitter，使相邻帧采不同的 VPL 子集，
// 配合 RSMTemporalResolve 跨帧累积后等效样本密度 × 历史帧数。
//
// 输入纹理槽：
//   slot 13 — u_RSMFlux     (VPL 出射通量)
//   slot 14 — u_RSMWorldPos (VPL 世界坐标)
//   slot 15 — u_RSMNormal   (VPL 世界法线，打包到 [0,1])
//   slot 16 — u_RSMDepth    (RSM 深度，用于跳过天空 VPL)
//   slot 18 — u_SceneDepth  (主 FBO 深度，重建接收点世界坐标)
//   slot 19 — u_ViewNormal  (主 FBO attachment2，view-space 法线)
//
// UBO：
//   binding 0 — Camera (u_LightSpaceMatrix, u_View)
//   binding 6 — RSMData (u_RSMHeader, u_RSMSamples)
//
// 额外 Uniforms：
//   u_InvViewProj  — inv(ViewProjection)，深度 → 世界坐标
//   u_FrameIndex   — 帧计数，驱动旋转 jitter
// =====================================================================

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
    vec4 u_RSMHeader;           // x=numSamples y=intensity z=sampleRadius w=enabled
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

const float PI = 3.14159265359;

float Hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

vec3 ReconstructWorldPos(vec2 uv, float depth)
{
    vec4 ndc    = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 worldH = u_InvViewProj * ndc;
    return worldH.xyz / worldH.w;
}

bool SampleRSMPoint(vec2 uv, out vec3 flux, out vec3 vplPos, out vec3 normal)
{
    ivec2 texSize = textureSize(u_RSMDepth, 0);
    if (texSize.x <= 0 || texSize.y <= 0) return false;

    ivec2 coord = clamp(ivec2(floor(uv * vec2(texSize))), ivec2(0), texSize - 1);

    float d = texelFetch(u_RSMDepth, coord, 0).r;
    if (d >= 0.9999) return false;

    flux   = texelFetch(u_RSMFlux,     coord, 0).rgb;
    vplPos = texelFetch(u_RSMWorldPos, coord, 0).rgb;
    normal = normalize(texelFetch(u_RSMNormal, coord, 0).rgb * 2.0 - 1.0);
    return true;
}

vec3 ComputeRSMIndirect(vec3 fragWorldPos, vec3 N, vec2 shadowUV)
{
    int numSamples = int(u_RSMHeader.x);
    if (numSamples <= 0) return vec3(0.0);

    float intensity    = u_RSMHeader.y;
    float sampleRadius = u_RSMHeader.z;

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
        if (dist2 < 1e-4) continue;

        vec3  dirN       = dir * inversesqrt(dist2);
        float vplFacing  = max(0.0, dot(vplNormal, dirN));
        float fragFacing = max(0.0, dot(N, -dirN));

        const float DIST_BIAS = 0.05;
        float geom   = vplFacing * fragFacing / (dist2 + DIST_BIAS);
        float contrib = min(geom, 4.0) * sampleWeight;

        indirect += vplFlux * contrib;
    }

    return indirect * (intensity / float(numSamples));
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
