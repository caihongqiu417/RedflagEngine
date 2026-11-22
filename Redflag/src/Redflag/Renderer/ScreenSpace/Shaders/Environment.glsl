#include "EnvironmentLayout.glsl"

// 一个方向落在绝对分量最大的立方体面上，再用其余两个分量找到面内坐标。
int cubeFace(vec3 d, out vec2 uv)
{
    vec3 a = abs(d);
    if (a.x >= max(a.y,a.z))
    {
        uv = vec2(d.x > 0. ? -d.z : d.z,-d.y) / a.x;
        uv = .5 * uv + .5; return d.x > 0. ? 0 : 1;
    }
    if (a.y >= a.z)
    {
        uv = vec2(d.x,d.y > 0. ? d.z : -d.z) / a.y;
        uv = .5 * uv + .5; return d.y > 0. ? 2 : 3;
    }
    uv = vec2(d.z > 0. ? d.x : -d.x,-d.y) / a.z;
    uv = .5 * uv + .5; return d.z > 0. ? 4 : 5;
}
vec3 cubeTap(int offset, int width, int face, ivec2 p)
{
    if (any(lessThan(p,ivec2(0))) || any(greaterThanEqual(p,ivec2(width))))
    {
        // 双线性过滤的一个邻居越过方形边缘时，沿同一方向在相邻面取色。
        vec2 uv;
        face = cubeFace(cubeDirection(face,(vec2(p)+.5)/float(width)),uv);
        p = clamp(ivec2(uv*width),ivec2(0),ivec2(width-1));
    }
    return environment[offset+(face*width+p.y)*width+p.x].rgb;
}
vec3 cubeLight(vec3 d, int offset, int width)
{
    vec2 uv;
    int face = cubeFace(d,uv);
    vec2 p = uv*width-.5, f = fract(p);
    ivec2 q = ivec2(floor(p));
    return mix(mix(cubeTap(offset,width,face,q),cubeTap(offset,width,face,q+ivec2(1,0)),f.x),
               mix(cubeTap(offset,width,face,q+ivec2(0,1)),cubeTap(offset,width,face,q+ivec2(1,1)),f.x),f.y);
}
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
