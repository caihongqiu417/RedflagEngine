uint probeIndex(uvec3 cell)
{
    return (cell.z * gridSize.y + cell.y) * gridSize.x + cell.x;
}
vec3 gatherDiffuseLight(Surface s)
{
    vec3 coord = clamp((s.p.xyz - gridOrigin.xyz) / gridStep.xyz, vec3(0), vec3(gridSize.xyz) - 1.001);
    uvec3 cell = uvec3(floor(coord));
    vec3 f = fract(coord);
    vec3 sum = vec3(0);
    float weights = 0.;
    for (uint i = 0u; i < 8u; ++i)
    {
        uvec3 off = uvec3(i & 1u, (i >> 1) & 1u, (i >> 2) & 1u), c = cell + off;
        vec3 probe = gridOrigin.xyz + vec3(c) * gridStep.xyz, ray = s.p.xyz - probe;
        float d = length(ray);
        vec3 wi = ray / max(d, EPS);
        vec3 w = mix(1. - f, f, vec3(off));
        float t = w.x * w.y * w.z;
        vec3 E = vec3(0);
        vec2 m = vec2(0);
        for (uint face = 0u; face < 6u; ++face)
        {
            Probe p = probes[probeIndex(c) * 6u + face];
            float cs = max(dot(s.n.xyz, axes[face]), 0.), cv = max(dot(wi, axes[face]), 0.);
            E += p.light.rgb * cs * cs;
            m += p.moments.xy * cv * cv;
        }
        float v = max(.0001, m.y - m.x * m.x);
        // 若表面比探针在这个方向上通常能照到的位置更远，中间可能隔着墙。让这个探针少贡献一些光。
        float beyond = max(0., d - m.x);
        float visibility = v / (v + beyond * beyond);
        t *= visibility * visibility * max(.035, dot(-wi, s.n.xyz));
        sum += E * t;
        weights += t;
    }
    // 探针保存的是照到表面的辐照度 E 除以 π。返回附近探针的加权平均，调用者再乘墙面的漫反射率。
    return sum / max(weights, 1e-8);
}
