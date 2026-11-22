#include "EnvironmentCube.glsl"

vec2 brdfCoefficients(float nv, float roughness)
{
    vec2 p = clamp(vec2(nv,roughness)*lutSize-.5,vec2(0),vec2(lutSize-1));
    ivec2 q = ivec2(p), next = min(q+1,ivec2(lutSize-1));
    int offset = reflectionOffset(reflectionLevels);
    vec2 f = fract(p);
    return mix(mix(environment[offset+q.y*lutSize+q.x].rg,environment[offset+q.y*lutSize+next.x].rg,f.x),
               mix(environment[offset+next.y*lutSize+q.x].rg,environment[offset+next.y*lutSize+next.x].rg,f.x),f.y);
}
vec3 specularEnvironment(vec3 r, float nv, float roughness, vec3 ks)
{
    // 最光滑的一层保存原来的天空。镜面的反射比例 ks 已由入射角确定，直接乘上这一方向的天空颜色。
    if (roughness == 0.) return ks * cubeLight(r, reflectionOffset(0), reflectionSize);
    float lod = roughness * float(reflectionLevels-1);
    int low = int(lod), high = min(low+1,reflectionLevels-1);
    vec3 a = cubeLight(r,reflectionOffset(low),reflectionSize >> low);
    vec3 b = cubeLight(r,reflectionOffset(high),reflectionSize >> high);
    vec2 brdf = brdfCoefficients(nv,roughness);
    return mix(a,b,fract(lod)) * (ks*brdf.x+brdf.y);
}
vec3 environmentFresnel(vec3 f0, float nv, float roughness)
{
    return f0 + (max(vec3(1.-roughness),f0)-f0)*pow(1.-nv,5.);
}
vec3 viewDirection(vec3 position)
{
    return projection[3][3] == 0. ? normalize(eye.xyz-position) : vec3(view[0][2],view[1][2],view[2][2]);
}
