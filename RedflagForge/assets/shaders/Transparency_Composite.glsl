#type vertex
#version 450 core

layout(std140, binding = 0) uniform Camera { mat4 u_ViewProjection; };
layout(location = 0) out vec2 v_UV;
layout(location = 1) flat out mat4 v_InverseVP;

void main()
{
    v_UV = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(v_UV * 2.0 - 1.0, 0.0, 1.0);
    v_InverseVP = inverse(u_ViewProjection);
}

#type fragment
#version 450 core

layout(std140, binding = 0) uniform Camera { mat4 u_ViewProjection; };
layout(location = 0) in vec2 v_UV;
layout(location = 1) flat in mat4 v_InverseVP;
layout(location = 0) out vec4 o_Color;
layout(binding = 0) uniform sampler2D u_Background;
layout(binding = 1) uniform sampler2D u_Surface;
layout(binding = 2) uniform sampler2D u_NormalDepth;
layout(binding = 3) uniform sampler2D u_Optics;
layout(binding = 4) uniform sampler2D u_OpaqueDepth;

vec3 WorldPosition(vec2 ndc, float z)
{
    vec4 point = v_InverseVP * vec4(ndc, z, 1.0);
    return point.xyz / point.w;
}

// 折射位置附近的四个像素按距离混色。若某个像素是玻璃前面的柱子，只把它那一份换回玻璃背后原本的颜色。
vec3 BackgroundAt(vec2 uv, ivec2 size, float glassDepth, vec3 original)
{
    vec2 pixel = clamp(uv * size - 0.5, vec2(0), vec2(size - 1));
    ivec2 begin = ivec2(pixel);
    vec2 f = fract(pixel);
    vec3 sum = vec3(0);
    for (int y = 0; y <= 1; ++y)
        for (int x = 0; x <= 1; ++x)
        {
            ivec2 tap = min(begin + ivec2(x, y), size - 1);
            vec3 color = texelFetch(u_OpaqueDepth, tap, 0).r < glassDepth ? original : texelFetch(u_Background, tap, 0).rgb;
            sum += color * (x == 0 ? 1.0 - f.x : f.x) * (y == 0 ? 1.0 - f.y : f.y);
        }
    return sum;
}

void main()
{
    ivec2 size = textureSize(u_Surface, 0);
    ivec2 pixel = ivec2(v_UV * size);
    vec4 sum = texelFetch(u_Surface, pixel, 0);
    if (sum.a == 0.0) discard;

    vec4 normalDepth = texelFetch(u_NormalDepth, pixel, 0) / sum.a;
    vec4 optics = texelFetch(u_Optics, pixel, 0);
    float ior = optics.y / sum.a;
    float thickness = optics.z / sum.a;
    vec3 background = texelFetch(u_Background, pixel, 0).rgb;

    if (ior > 1.0 && thickness > 0.0 && dot(normalDepth.xyz, normalDepth.xyz) > 1e-8)
    {
        vec2 ndc = v_UV * 2.0 - 1.0;
        vec3 hit = WorldPosition(ndc, normalDepth.w * 2.0 - 1.0);
        vec3 incident = normalize(WorldPosition(ndc, 1.0) - WorldPosition(ndc, -1.0));
        vec3 normal = normalize(normalDepth.xyz);
        normal = faceforward(normal, incident, normal);
        vec3 bent = refract(incident, normal, 1.0 / ior);

        // 厚度沿表面法线量。光线斜着穿过玻璃，走过的距离会更长。
        vec3 exitPoint = hit + bent * (thickness / max(0.001, -dot(bent, normal)));
        vec4 clip = u_ViewProjection * vec4(exitPoint, 1.0);
        vec2 uv = clip.xy / clip.w * 0.5 + 0.5;
        if (clip.w > 0.0 && all(greaterThanEqual(uv, vec2(0))) && all(lessThan(uv, vec2(1))))
            background = BackgroundAt(uv, size, normalDepth.w, background);
    }

    float transmittance = exp(optics.x);
    o_Color = vec4(mix(sum.rgb / sum.a, background, transmittance), 1.0);
}
