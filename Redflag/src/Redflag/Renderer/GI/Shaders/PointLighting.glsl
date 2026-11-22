#include "LightSource.glsl"

// glTF 材质的 G 通道表示粗糙度，B 通道表示金属度。传给下面两式的底色已转成线性 RGB。
vec3 diffuseReflectance(vec3 color, float metallic)
{
    return color * (1. - metallic);
}

// 世界空间内，V 从表面指向眼睛，L 从表面指向光源。返回值已乘法线与 L 的夹角余弦，调用者再乘到达表面的光强。见 glTF 2.0 附录 B。
vec3 reflectedLight(vec3 color, float roughness, float metallic, vec3 N, vec3 V, vec3 L)
{
    float nv = dot(N, V), nl = dot(N, L);
    if (nv <= 0. || nl <= 0.) return vec3(0);
    vec3 H = normalize(V + L);
    float nh = clamp(dot(N, H), 0., 1.), vh = clamp(dot(V, H), 0., 1.);
    float a = max(roughness * roughness, .002); // 粗糙度为零时仍给高光保留很小的宽度，避免下方的分布函数除以零。
    float a2 = a * a;
    float den = (1. - nh * nh) + nh * nh * a2;
    float distribution = a2 / (PI * den * den);
    float visibility = .5 / (nl * sqrt(nv * nv * (1. - a2) + a2) +
                              nv * sqrt(nl * nl * (1. - a2) + a2));
    float grazing = pow(1. - vh, 5.);
    float dielectricF = .04 + .96 * grazing;
    vec3 F = mix(vec3(dielectricF), mix(color, vec3(1), grazing), metallic);
    return nl * ((1. - dielectricF) * diffuseReflectance(color, metallic) / PI + F * distribution * visibility);
}
