// glTF 材质的 G 通道表示粗糙度，B 通道表示金属度。传给下面两式的底色已转成线性 RGB。
vec3 diffuseReflectance(vec3 color, float metallic)
{
    return color * (1. - metallic);
}

// V、L 分别从表面指向眼睛和灯。函数已算入斜照时光在表面上摊开的影响，调用者再乘灯的强度与距离衰减。见 glTF 2.0 附录 B。
vec3 reflectedPointLight(vec3 color, float roughness, float metallic, vec3 N, vec3 V, vec3 L)
{
    float nv = dot(N, V), nl = dot(N, L);
    if (nv <= 0. || nl <= 0.) return vec3(0);
    vec3 H = normalize(V + L);
    float nh = clamp(dot(N, H), 0., 1.), vh = clamp(dot(V, H), 0., 1.);
    float a = max(roughness * roughness, .002); // 点光源照完全光滑的镜面只在一个方向有值，保留一个有限宽度。
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

float pointAttenuation(float distanceToLight, float radius)
{
    float d2 = distanceToLight * distanceToLight;
    float fade = radius > 0. ? max(0., 1. - d2 / (radius * radius)) : 1.;
    return fade * fade / d2;
}
