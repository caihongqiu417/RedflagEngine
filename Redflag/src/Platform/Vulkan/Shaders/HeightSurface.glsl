// T 和 B 分别对应贴图横跨、纵跨一整圈时，网格表面在模型坐标中走过的距离。
mat3 textureFrame(vec3 position, vec3 normal, vec2 uv)
{
    vec3 p = dFdx(position), q = dFdy(position);
    vec2 u = dFdx(uv), v = dFdy(uv);
    float det = u.x * v.y - u.y * v.x;
    if (abs(det) < 1e-15) return mat3(0);
    return mat3((p*v.y - q*u.y)/det, (q*u.x - p*v.x)/det, normalize(normal));
}

vec2 reliefUV(sampler2D heights, vec2 uv, vec3 position, vec3 normal, vec3 toEye)
{
    if (heightMode() != 2u) return uv;
    mat3 axes = textureFrame(position, normal, uv);
    if (abs(determinant(axes)) < 1e-12) return uv;
    vec2 dx = dFdx(uv), dy = dFdy(uv);
    vec3 ray = inverse(axes) * normalize(toEye);
    if (ray.z < .05) return uv; // 接近擦边时视线跨过太多纹理，平滑淡出视差。
    vec2 travel = ray.xy / ray.z * heightRange() * smoothstep(.05, .15, ray.z);
    // 正面对着表面用 8 层，斜看时最多用 24 层。层越密，较深沟槽的交点越准。
    int layers = int(mix(24.0, 8.0, clamp(ray.z, 0.0, 1.0)));
    float stepDepth = 1.0 / float(layers);
    float above = 1.0 - textureGrad(heights, uv, dx, dy).r;
    if (above <= 0.0) return uv;
    for (int layer = 1; layer <= layers; ++layer)
    {
        float z = float(layer) * stepDepth;
        vec2 sampleUV = uv - z * travel;
        float below = 1.0 - textureGrad(heights, sampleUV, dx, dy).r - z;
        if (below <= 0.0)
        {
            float hit = z - stepDepth + stepDepth * above / (above - below);
            return uv - hit * travel;
        }
        above = below;
    }
    return uv - travel;
}

vec3 bumpNormal(sampler2D heights, vec2 uv, mat3 axes, vec3 normal, vec2 dx, vec2 dy)
{
    // 向纹理两边各看一个像素，用高度差求坡度。反转 UV 的模型仍沿实际切线方向倾斜。
    if (abs(determinant(axes)) < 1e-12) return normal;
    float level = clamp(textureQueryLod(heights, uv).y, 0.0, float(textureQueryLevels(heights)-1));
    vec2 texel = 1.0 / vec2(textureSize(heights, int(level)));
    vec2 grad = vec2(
        textureGrad(heights, uv+vec2(texel.x,0), dx, dy).r - textureGrad(heights, uv-vec2(texel.x,0), dx, dy).r,
        textureGrad(heights, uv+vec2(0,texel.y), dx, dy).r - textureGrad(heights, uv-vec2(0,texel.y), dx, dy).r
    ) * (heightRange() / (2.0 * texel));
    return normalize(normal - transpose(inverse(axes)) * vec3(grad,0));
}
