#include "../../GI/Shaders/SceneTypes.glsl"

layout(std430, binding = 5) readonly buffer Nodes { Node node[]; };
layout(std430, binding = 6) readonly buffer Triangles { Triangle tri[]; };
layout(std430, binding = 7) readonly buffer Lamps { Light lights[]; };
layout(std430, binding = 8) readonly buffer SceneInfo {
    mat4 inverseCamera;
    vec4 cameraPosition, gridOrigin, gridStep;
    uvec4 imageSize, gridSize;
};

#include "../../GI/Shaders/TextureFace.glsl"
struct Emission { vec4 color; uvec4 image; };
struct Probe { vec4 light, moments; };
layout(std430, binding = 9) readonly buffer TextureFaces { TextureFace faces[]; };
layout(std430, binding = 10) readonly buffer MaterialPixels { uint texels[]; };
layout(std430, binding = 11) readonly buffer Emitters { Emission emitters[]; };
layout(std430, binding = 12) readonly buffer Probes { Probe probes[]; };
#ifdef RF_ALPHA_MASK
#define RF_MASK_BINDING 13
#endif

const float EPS = .001; // 光线起点沿表面法线移开这段场景距离，避免打回出发的三角形。
vec3 emittedAtHit;
#define RF_EMISSION
#include "../../GI/Shaders/MaterialTexture.glsl"
#include "../../GI/Shaders/PointLighting.glsl"
#include "../../GI/Shaders/Trace.glsl"
#include "../../GI/Shaders/RaySurface.glsl"
#include "../../GI/Shaders/ProbeAxes.glsl"
#include "../../GI/Shaders/ProbeSample.glsl"

bool sceneReflection(vec3 origin, vec3 direction, float distance, out vec3 incoming)
{
    Surface hit = raySurface(origin, direction, distance);
    if (hit.p.w == 0.) return false;

    // 镜子里看到的是物体朝镜面送来的光。算高光时，视线沿反射光线返回镜面。
    vec3 n = hit.n.xyz, v = -direction;
    incoming = emittedAtHit;
    for (uint lamp = 0; lamp < extent.w; ++lamp)
    {
        Light light = lights[lamp];
        vec3 l;
        float r;
        float radius = depthRange.z != 0. ? temporal.z : light.intensity.w;
        float fade = sampleLight(light.p, radius, hit.p.xyz, l, r);
        float nl = dot(n, l);
        if (nl <= 0. || fade == 0.) continue;
        if (lightCastsShadow(light.p) && surfaceInShadow(hit.p.xyz, n, l, r)) continue;
        vec3 response = depthRange.z != 0. ? hit.rho.rgb * (nl / PI)
            : reflectedLight(hit.rho.rgb, hit.n.w, hit.rho.w, n, v, l);
        incoming += response * light.intensity.rgb * fade;
    }

    vec3 diffuse = diffuseReflectance(hit.rho.rgb, hit.rho.w);
    if (useProbes != 0) incoming += diffuse * gatherDiffuseLight(hit);
    if (useDiffuseSky != 0 || useEnvironment != 0)
    {
        float nv = clamp(dot(n, v), 0., 1.);
        vec3 f0 = mix(vec3(.04), hit.rho.rgb, hit.rho.w);
        vec3 ks = environmentFresnel(f0, nv, hit.n.w);
        if (useDiffuseSky != 0) incoming += (1. - ks) * diffuse * cubeLight(n, 0, diffuseSize);
        if (useEnvironment != 0) incoming += specularEnvironment(reflect(-v, n), nv, hit.n.w, ks);
    }
    return true;
}
