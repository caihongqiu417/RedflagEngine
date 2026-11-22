vec3 panorama(vec3 direction, float solidAngle)
{
    vec2 uv = vec2(atan(direction.z, direction.x) / (2. * pi), asin(clamp(direction.y,-1.,1.)) / pi) + .5;
    // 赤道的纹素比极点覆盖更大的立体角。一个采样点代表的范围越大，读取越模糊的一层。
    float texelAngle = 2. * pi * pi * max(length(direction.xz), 1. / size.y) / float(size.x * size.y);
    int mip = int(max(0., floor(.5 * log2(max(1., solidAngle / texelAngle)) + .5)));
    ivec2 extent = size;
    int offset = 0;
    for (int level = 0; level < mip && any(greaterThan(extent,ivec2(1))); ++level)
    {
        offset += extent.x * extent.y;
        extent = max(extent / 2, ivec2(1));
    }
    vec2 p = uv * vec2(extent) - .5;
    ivec2 q = ivec2(floor(p));
    ivec2 x = (ivec2(q.x, q.x+1) + extent.x) % extent.x;
    ivec2 y = clamp(ivec2(q.y, q.y+1), ivec2(0), ivec2(extent.y-1));
    vec2 f = fract(p);
    return mix(mix(environment[offset+y.x*extent.x+x.x].rgb, environment[offset+y.x*extent.x+x.y].rgb, f.x),
               mix(environment[offset+y.y*extent.x+x.x].rgb, environment[offset+y.y*extent.x+x.y].rgb, f.x), f.y);
}
