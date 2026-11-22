vec3 readTexel(uvec4 image, ivec2 pixel, bool srgb)
{
    ivec2 size = ivec2(image.yz);
    pixel = (pixel % size + size) % size;
    vec3 s = unpackUnorm4x8(texels[image.x + uint(pixel.y * size.x + pixel.x)]).rgb;
    if (!srgb) return s;
    return mix(pow((s + .055) / 1.055, vec3(2.4)), s / 12.92, lessThanEqual(s,vec3(.04045)));
}

vec3 sampleMap(uvec4 image, vec2 uv, bool srgb)
{
    vec2 p = fract(uv) * vec2(image.yz) - .5, w = fract(p);
    ivec2 q = ivec2(floor(p));
    vec3 a = mix(readTexel(image,q,srgb), readTexel(image,q+ivec2(1,0),srgb), w.x);
    vec3 b = mix(readTexel(image,q+ivec2(0,1),srgb), readTexel(image,q+ivec2(1,1),srgb), w.x);
    return mix(a,b,w.y);
}
