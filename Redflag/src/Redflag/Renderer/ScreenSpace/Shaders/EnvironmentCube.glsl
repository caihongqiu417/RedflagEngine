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
