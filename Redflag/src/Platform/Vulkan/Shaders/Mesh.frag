#version 450
#include "Object.glsl"
#include "MaterialChannels.glsl"
#ifndef TRANSPARENT
#ifdef LIGHTING_SURFACES
#define SUBPIXEL_MASK
#endif
#include "Cutout.glsl"
#endif

layout(location = 0) in vec2 v_TexCoord;
layout(set = 0, binding = 0) uniform sampler2D u_BaseColorMap;
layout(set = 2, binding = 0) uniform sampler2D u_OpacityMap;

#ifdef LIGHTING_SURFACES
layout(location = 1) in vec3 v_LocalPosition;
layout(location = 2) in vec3 v_LocalNormal;
layout(set = 1, binding = 0) uniform sampler2D u_NormalMap;
layout(location = 0) out vec4 o_Albedo;
layout(location = 1) out vec4 o_NormalDepth;
#ifdef TRANSPARENT
layout(location = 3) in vec2 v_EmissiveUV;
layout(location = 2) out vec4 o_Optics;
#else
layout(set = 3, binding = 0) uniform sampler2D u_MetallicRoughnessMap;
#endif

vec3 worldNormal()
{
    vec3 n = v_LocalNormal;
    vec3 p = dFdx(v_LocalPosition), q = dFdy(v_LocalPosition);
    if (dot(n, n) == 0.) n = cross(p, q);
    n = normalize(n);
    // 从贴图坐标在三角形上的变化求切线，也保留镜像 UV 的朝向。
    vec2 u = dFdx(v_TexCoord), v = dFdy(v_TexCoord);
    float det = u.x * v.y - u.y * v.x;
    if ((u_Object.MaterialFlags & 1u) != 0u && abs(det) > 1e-15) {
        vec3 t = (p * v.y - q * u.y) / det;
        vec3 b = (q * u.x - p * v.x) / det;
        t = normalize(t - n * dot(n, t));
        b = sign(dot(cross(n, t), b)) * cross(n, t);
        vec3 sampled = texture(u_NormalMap, v_TexCoord).xyz * 2. - 1.;
        n = mat3(t, b, n) * sampled;
    }
    mat3 normal = mat3(u_Object.NormalMatrix[0], u_Object.NormalMatrix[1], u_Object.NormalMatrix[2],
                       u_Object.NormalMatrix[3], u_Object.NormalMatrix[4], u_Object.NormalMatrix[5],
                       u_Object.NormalMatrix[6], u_Object.NormalMatrix[7], u_Object.NormalMatrix[8]);
    return normalize(normal * n);
}
#else
layout(location = 0) out vec4 o_Color;
layout(location = 1) out int o_EntityID;
#endif

void main()
{
    if ((u_Object.MaterialFlags & 2u) != 0u && !gl_FrontFacing) discard;
    vec4 base = texture(u_BaseColorMap, v_TexCoord);
#if defined(LIGHTING_SURFACES) && !defined(TRANSPARENT)
    vec3 n = worldNormal();
#endif
#ifndef TRANSPARENT
    if (u_Object.AlphaCutoff >= 0. && !coversSample(u_BaseColorMap, u_OpacityMap, v_TexCoord, base.a)) discard;
#endif
#ifdef LIGHTING_SURFACES
    // 底色图按 sRGB 采样，硬件已把 RGB 还原成线性亮度。透明度始终按原来的比例读取。
    vec3 linear = base.rgb;
#ifdef TRANSPARENT
    float alpha = clamp(base.a * u_Object.Color.a * texture(u_OpacityMap, v_TexCoord).r, 0., 1.);
    if (alpha == 0.) discard;
    float weight = alpha * max(.01, pow(1. - gl_FragCoord.z * .9, 3.));
    if ((u_Object.MaterialFlags & 8u) != 0u) {
        // 补画玻璃自身的光。此时原本放法线贴图的位置换成了按 sRGB 读取的发光贴图。
        o_Albedo = vec4(u_Object.Color.rgb * texture(u_NormalMap, v_EmissiveUV).rgb * weight, 0.);
        o_NormalDepth = o_Optics = vec4(0.);
        return;
    }
    o_Albedo = vec4(linear * u_Object.Color.rgb, 1.) * weight;
    o_NormalDepth = vec4(worldNormal(), gl_FragCoord.z) * weight;
    // 两层玻璃各透过一半背景光，叠在一起就剩四分之一。这里相加的是透过率的对数，合成时用 exp 还原这个乘积。
    o_Optics = vec4(log(max(1. - alpha, 1e-6)), u_Object.RefractionIndex * weight,
                    u_Object.Thickness * weight, 0.);
#else
    vec2 material = unpackUnorm2x16(u_Object.MaterialFactors);
    // glTF 的蓝色是金属度，绿色是粗糙度。两者都是数据，不做底色使用的 sRGB 转换。
    if ((u_Object.MaterialFlags & 4u) != 0u)
        material *= texture(u_MetallicRoughnessMap, v_TexCoord).bg;
    float packed = encodeMaterial(material);
    // 负号标出通过深度测试的发光材质。普通墙与灯牌恰好共面时，只让真正画在前面的那一层发光。
    o_Albedo = vec4(linear * u_Object.Color.rgb, (u_Object.MaterialFlags & 8u) != 0u ? -packed : packed);
    o_NormalDepth = vec4(n, gl_FragCoord.z);
#endif
#else
    // 面板的颜色图不会自动转成 sRGB。这里把线性 RGB 换成屏幕显示用的颜色，透明度单独取原值。
    vec3 display = mix(1.055 * pow(base.rgb,vec3(1./2.4)) - .055, 12.92 * base.rgb,
                       lessThanEqual(base.rgb,vec3(.0031308)));
    o_Color = vec4(display,base.a) * u_Object.Color;
    o_EntityID = u_Object.EntityID;
#endif
}
