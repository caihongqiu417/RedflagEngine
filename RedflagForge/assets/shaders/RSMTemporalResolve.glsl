// =====================================================================
// RSMTemporalResolve.glsl — RSM 间接光时域累积 Pass
//
// 将本帧 RSMIndirect 结果与上一帧 resolved history 做时域融合，
// 通过反投影找到当前像素在上一帧中的屏幕位置，
// 越界或第一帧时完全使用当前帧，否则按 alpha 混合历史。
//
// 输入纹理槽：
//   slot 0 — u_Current   (本帧 RSMIndirect 结果, RGBA16F)
//   slot 1 — u_History   (上一帧 resolved 结果, RGBA16F)
//   slot 2 — u_Depth     (主 FBO 深度，重建世界坐标用于反投影)
//
// Uniforms：
//   u_InvViewProj  — inv(当前帧 ViewProjection)
//   u_PrevViewProj — 上一帧 ViewProjection
//   u_BlendAlpha   — 历史混合权重，0.9 = 90% history + 10% current
//   u_ResetHistory — 1 = 第一帧或 viewport resize，强制使用 current
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

layout(location = 0) out vec4 o_Resolved;

in vec2 v_TexCoord;

layout(binding = 0) uniform sampler2D u_Current;
layout(binding = 1) uniform sampler2D u_History;
layout(binding = 2) uniform sampler2D u_Depth;

layout(std140, binding = 8) uniform RSMTemporalResolveParams
{
    mat4  u_InvViewProj;
    mat4  u_PrevViewProj;
    float u_BlendAlpha;
    int   u_ResetHistory;
    float _taaP0, _taaP1;  // std140 padding to vec4 boundary
};

vec3 ReconstructWorldPos(vec2 uv, float depth)
{
    vec4 ndc    = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 worldH = u_InvViewProj * ndc;
    return worldH.xyz / worldH.w;
}

void main()
{
    vec3 current = texture(u_Current, v_TexCoord).rgb;

    // 第一帧或 viewport resize 时直接输出当前帧
    if (u_ResetHistory != 0)
    {
        o_Resolved = vec4(current, 1.0);
        return;
    }

    float depth = texture(u_Depth, v_TexCoord).r;

    // 背景像素（天空）：没有几何，历史无意义
    if (depth >= 0.9999)
    {
        o_Resolved = vec4(current, 1.0);
        return;
    }

    // 重建世界坐标，反投影到上一帧屏幕空间
    vec3 worldPos  = ReconstructWorldPos(v_TexCoord, depth);
    vec4 prevClip  = u_PrevViewProj * vec4(worldPos, 1.0);
    vec2 prevNDCxy = prevClip.xy / max(prevClip.w, 1e-6);
    vec2 prevUV    = prevNDCxy * 0.5 + 0.5;

    // 反投影超出上一帧视锥或在相机后方 — 拒绝历史
    if (prevClip.w <= 0.0 ||
        prevUV.x < 0.0 || prevUV.x > 1.0 ||
        prevUV.y < 0.0 || prevUV.y > 1.0)
    {
        o_Resolved = vec4(current, 1.0);
        return;
    }

    vec3 history = texture(u_History, prevUV).rgb;

    // 邻域 clamp：把 history 限制在当前帧 3×3 邻域的 AABB 内，防止 ghosting
    vec3 cMin = current, cMax = current;
    ivec2 sz = textureSize(u_Current, 0);
    vec2 texel = 1.0 / vec2(sz);
    for (int dy = -1; dy <= 1; ++dy)
    for (int dx = -1; dx <= 1; ++dx)
    {
        vec3 s = texture(u_Current, v_TexCoord + vec2(dx, dy) * texel).rgb;
        cMin = min(cMin, s);
        cMax = max(cMax, s);
    }
    history = clamp(history, cMin, cMax);

    // 时域混合：90% history + 10% current（默认）
    o_Resolved = vec4(mix(current, history, u_BlendAlpha), 1.0);
}
