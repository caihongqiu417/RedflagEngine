// 点光源存世界位置，方向光存从表面指向光源的单位向量。方向光开启阴影时 w = -1，关闭时为 -2。
bool isDirectionalLight(vec4 source)
{
    return source.w < 0.;
}

bool lightCastsShadow(vec4 source)
{
    return source.w != -2.;
}

// 返回距离衰减，并给出阴影射线的方向和最远距离。方向光在场景各处照过来的方向、强度都相同。
float sampleLight(vec4 source, float radius, vec3 position, out vec3 toLight, out float distanceToLight)
{
    if (isDirectionalLight(source)) {
        toLight = source.xyz;
        distanceToLight = 3.402823466e38; // float 最大有限值，场景里的任何遮挡物都在查找范围内。
        return 1.;
    }

    vec3 delta = source.xyz - position;
    distanceToLight = length(delta);
    if (distanceToLight < .001) { // 延用点光源的最小距离（场景单位），避开灯心处没有方向的射线。
        toLight = vec3(0);
        return 0.;
    }

    toLight = delta / distanceToLight;
    float d2 = distanceToLight * distanceToLight;
    float fade = radius > 0. ? max(0., 1. - d2 / (radius * radius)) : 1.;
    return fade * fade / d2;
}
