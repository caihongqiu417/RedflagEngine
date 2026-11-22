#include "ProbeSample.glsl"

void updateProbe(uint id)
{
    uint face = id % 6u, k = id / 6u;
    if (k >= gridSize.x * gridSize.y * gridSize.z)
        return;
    uvec3 cell = uvec3(k % gridSize.x, (k / gridSize.x) % gridSize.y, k / (gridSize.x * gridSize.y));
    vec3 p = gridOrigin.xyz + vec3(cell) * gridStep.xyz;
    vec3 sum = vec3(0);
    vec2 moments = vec2(0);
    for (uint j = 0u; j < samples; ++j)
    {
        float t;
        Surface y = raySurface(p, hemisphere(axes[face]), t);
        if (y.p.w > 0.)
        {
            sum += incomingAt(y);
            if (frame > 0u && (base & 1u) != 0u) {
                // 上轮探针给出到达墙面的光。材质最多把入射光全部反回去，强灯的 HDR 亮度原样保留。
                sum += clamp(diffuseReflectance(y.rho.rgb, y.rho.w), vec3(0), vec3(1)) * gatherDiffuseLight(y);
            }
        }
        t = min(t, 2. * length(gridStep.xyz));
        moments += vec2(t, t * t);
    }
    Probe outP = Probe(vec4(sum / float(samples), 0), vec4(moments / float(samples), 0, 0));
    // 从第二轮起，实时绘制给新算的光照和距离两成权重。调小这个比例，画面闪动更轻，新结果也要更多轮才显出来。
    // 烘焙直接保存当轮结果。
    if (frame > 0u && (base & 2u) != 0u)
    {
        outP.light = mix(probes[id].light, outP.light, .2);
        outP.moments = mix(probes[id].moments, outP.moments, .2);
    }
    nextProbes[id] = outP;
}
