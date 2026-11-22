#ifdef RF_SCENE_REFLECTION
Surface raySurface(vec3 o, vec3 d, inout float distance)
{
#else
Surface raySurface(vec3 o, vec3 d, out float distance)
{
#ifdef RF_PROBE_SKY
    distance = 3.402823466e38; // 连远处的屋顶也要检查，越过所有几何才算到达天空。
#else
    distance = 1000.; // 查找前方 1000 个场景单位以内的表面。更远的物体不会被这条光线找到。
#endif
#endif
    vec2 weights;
    int hit = trace(o, d, distance, false, weights);
    if (hit < 0)
        return Surface(vec4(0), vec4(0), vec4(0));
    Triangle t = tri[hit];
#ifdef RF_ALPHA_MASK
    // 负号只在求交时表示镂空材质。留下的表面按同一组 UV 读取底色、粗糙度和自发光。
    t.albedo.w = abs(t.albedo.w);
#endif
    vec3 n = normalize(cross(t.b.xyz - t.a.xyz, t.c.xyz - t.a.xyz));
#ifdef RF_EMISSION
    // 三角形的 a.w 为 0 时透光，为 1 时只反射光。其余值指向它所属的发光材质。
    emittedAtHit = vec3(0);
    if (t.a.w >= 2.) {
        Emission emitter = emitters[uint(t.a.w) - 2u];
        if (dot(n,d) < 0. || emitter.color.w != 0.) {
            emittedAtHit = emitter.color.rgb;
            if (emitter.image.y != 0u && t.albedo.w > 0.) {
                TextureFace face = faces[uint(t.albedo.w) - 1u];
                if (face.uv2.z > 0.) face = faces[uint(face.uv2.z) - 1u];
                vec2 uv = face.uv01.xy * (1. - weights.x - weights.y) + face.uv01.zw * weights.x + face.uv2.xy * weights.y;
                emittedAtHit *= sampleMap(emitter.image, uv, true);
            }
        }
    }
#endif
    vec3 color = t.albedo.rgb;
    vec2 factors = vec2(t.b.w, t.c.w);
    // 有贴图时，编号从 1 开始。用命中点的 UV 读取底色、粗糙度和金属度，再乘上材质自身的值。
    if (t.albedo.w > 0.) {
        TextureFace face = faces[uint(t.albedo.w) - 1u];
        vec2 uv = face.uv01.xy * (1. - weights.x - weights.y) + face.uv01.zw * weights.x + face.uv2.xy * weights.y;
        if (face.image.y != 0u) color *= sampleMap(face.image, uv, true);
        if (face.metallicRoughness.y != 0u) factors *= sampleMap(face.metallicRoughness, uv, false).gb;
    }
    return Surface(vec4(o + d * distance, 1), vec4(dot(n, d) > 0. ? -n : n, factors.x), vec4(color, factors.y));
}
