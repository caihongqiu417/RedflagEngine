/*
 * 用同一表面上一帧的反射光减轻本帧的噪点。相机移动后，先算出表面上这个点在旧画面里的位置。
 * 这里只用相机矩阵回找位置。物体自己移动时，仍可能带上原处的颜色。
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
    float _taaP0, _taaP1;
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

    if (u_ResetHistory != 0)
    {
        o_Resolved = vec4(current, 1.0);
        return;
    }

    float depth = texture(u_Depth, v_TexCoord).r;

    // 天空没有接收反射光的表面。
    if (depth >= 0.9999)
    {
        o_Resolved = vec4(current, 1.0);
        return;
    }

    // 把当前像素还原成场景中的点，再投影到上一帧。
    vec3 worldPos  = ReconstructWorldPos(v_TexCoord, depth);
    vec4 prevClip  = u_PrevViewProj * vec4(worldPos, 1.0);
    // 检查上一帧的左右、上下和远近边界。点在范围外时直接取本帧颜色。
    if (prevClip.w <= 0.0 || any(greaterThan(abs(prevClip.xyz), vec3(prevClip.w))))
    {
        o_Resolved = vec4(current, 1.0);
        return;
    }

    vec2 prevUV = (prevClip.xy / prevClip.w) * 0.5 + 0.5;
    vec3 history = texture(u_History, prevUV /* 同一个世界点在上一帧的位置 */).rgb;

    // 旧颜色可能来自刚移走的物体。用本帧周围 3×3 个像素的颜色范围限制它，减轻拖影。
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

    o_Resolved = vec4(mix(current, history, u_BlendAlpha /* 0.9 表示保留九成旧颜色 */), 1.0);
}
