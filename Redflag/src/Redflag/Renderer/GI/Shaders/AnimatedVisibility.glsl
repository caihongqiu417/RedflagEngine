// 灯编号指向这一盏方向光的矩阵、图宽、每格的世界尺寸，以及逐像素的角色深度。
layout(std430, binding = RF_VISIBILITY_BINDING) readonly buffer AnimatedShadows { uint shadowWords[]; };
#include "CascadeVisibility.glsl"

float compareShadow(uint first, int side, ivec2 p, float receiver)
{
    if (any(lessThan(p, ivec2(0))) || any(greaterThanEqual(p, ivec2(side)))) return 1.;
    float caster = uintBitsToFloat(shadowWords[first + uint(p.y * side + p.x)]);
    return float(receiver <= caster);
}

float animatedVisibility(uint lamp, vec3 world, vec3 normal, vec3 toLight)
{
    if (usesCascades(lamp)) return cascadeVisibility(lamp,world,normal,toLight);
    uint first = shadowWords[lamp];
    mat4 lightVP;
    for (int c=0; c<4; ++c)
        for (int r=0; r<4; ++r)
            lightVP[c][r] = uintBitsToFloat(shadowWords[first + uint(4*c+r)]);
    int side = int(shadowWords[first + 16u]);
    float texel = uintBitsToFloat(shadowWords[first + 17u]);

    // 接收点沿法线偏移最多一格，减轻斜面把自己遮黑的问题。
    vec3 sampleAt = world + normal * texel * (1. - clamp(dot(normal, toLight), 0., 1.));
    vec3 ndc = (lightVP * vec4(sampleAt, 1.)).xyz;
    vec2 p = (ndc.xy * vec2(.5,-.5) + .5) * float(side) - .5;
    ivec2 base = ivec2(floor(p));
    vec2 w = fract(p);
    // 加减一格深度会让阴影离开脚底。这里只放宽单精度浮点变换产生的舍入误差。
    float receiver = ndc.z * .5 + .5 - 2e-6;
    first += 18u;
    float top = mix(compareShadow(first,side,base,receiver), compareShadow(first,side,base+ivec2(1,0),receiver),w.x);
    float bottom = mix(compareShadow(first,side,base+ivec2(0,1),receiver), compareShadow(first,side,base+ivec2(1),receiver),w.x);
    return mix(top,bottom,w.y);
}
