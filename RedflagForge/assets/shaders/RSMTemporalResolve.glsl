/*
 * 把当前帧与上一帧的间接光混合，减轻随机采样的噪点。
 * 相机移动后，同一个表面会出现在不同像素，所以先找出它上一帧对应的屏幕位置。
 * 找不到有效位置，或者视口刚改变尺寸时，就直接使用当前帧。
 * 历史颜色先限制在当前像素周围的颜色范围内，再按权重混合，避免旧颜色拖得太远。
 * 这里只补偿了相机变化，没有记录物体自身的运动，因此移动物体仍可能出现拖影。
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

/* 当前深度按 OpenGL 约定从 [0,1] 还原到 NDC [-1,1]，再做齐次除法。 */
vec3 ReconstructWorldPos(vec2 uv /* 屏幕 [0,1] */, float depth /* 深度附件值 */)
{
    vec4 ndc    = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 worldH = u_InvViewProj * ndc;
    return worldH.xyz / worldH.w;
}

void main()
{
    vec3 current = texture(u_Current, v_TexCoord).rgb;

    // 第一帧、视口变化或场景/光照参数改变后，直接输出当前帧。
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

    // ---- 重投影到上一帧 ----
    // 只补偿相机变化；没有运动矢量时，移动表面的历史位置可能不准确。
    vec3 worldPos  = ReconstructWorldPos(v_TexCoord, depth);
    vec4 prevClip  = u_PrevViewProj * vec4(worldPos, 1.0);
    // OpenGL 裁剪范围为 -w≤x,y,z≤w；屏幕外、相机后方及近远平面外都没有有效历史。
    if (prevClip.w <= 0.0 || any(greaterThan(abs(prevClip.xyz), vec3(prevClip.w))))
    {
        o_Resolved = vec4(current, 1.0);
        return;
    }

    vec2 prevUV = (prevClip.xy / prevClip.w) * 0.5 + 0.5;
    vec3 history = texture(u_History, prevUV).rgb;

    // ---- 限制上一帧的颜色 ----
    // 先找当前像素周围 3x3 区域的最暗和最亮颜色。
    // 如果历史颜色已经超出这个范围，就把它拉回范围内，减轻旧画面留下的拖影。
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

    // ---- 历史混合 ----
    // BlendAlpha 是历史权重；0.9 表示 90% history + 10% current。
    o_Resolved = vec4(mix(current, history, u_BlendAlpha), 1.0);
}
