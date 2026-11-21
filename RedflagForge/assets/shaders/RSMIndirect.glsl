/*
 * 估算白色表面能收到多少反射光。以白地板为例，附近受光的红墙也会把它照红。
 * 从相机深度找出地板的位置，再到灯光看见的表面中采样。主绘制会把结果乘上地板本身的颜色。
 */

#type vertex
#version 450 core

layout(location = 0) in vec2 a_Position;

out vec2 v_TexCoord;

void main()
{
    v_TexCoord  = a_Position * 0.5 + 0.5;
    gl_Position = vec4(a_Position, 0.0, 1.0);
}

#type fragment
#version 450 core

layout(location = 0) out vec4 o_Indirect;

in vec2 v_TexCoord;

layout(std140, binding = 0) uniform Camera
{
    mat4 u_ViewProjection;
    vec4 u_ViewPosition;
    mat4 u_LightSpaceMatrix;
    vec4 u_ShadowParams;
    vec4 u_ShadowParams2;
    mat4 u_View;
};

layout(std140, binding = 6) uniform RSMData
{
    vec4 u_RSMHeader;       // 依次是样本数、间接光强度、采样半径和灯光覆盖的宽度。
    vec4 u_RSMSamples[128]; // xy 是单位圆盘内的偏移，z 是样本的权重。
};

layout(binding = 13) uniform sampler2D u_RSMFlux;
layout(binding = 14) uniform sampler2D u_RSMWorldPos;
layout(binding = 15) uniform sampler2D u_RSMNormal;
layout(binding = 16) uniform sampler2D u_RSMDepth;
layout(binding = 18) uniform sampler2D u_SceneDepth;
layout(binding = 19) uniform sampler2D u_ViewNormal;

layout(std140, binding = 7) uniform RSMIndirectParams
{
    mat4 u_InvViewProj;
    int  u_FrameIndex;
    int  _rsmIPad0, _rsmIPad1, _rsmIPad2;
};

float Hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// 深度图的 0 到 1 对应 OpenGL 投影后的 -1 到 1，逆变换后除以 w 得到世界坐标。
vec3 ReconstructWorldPos(vec2 uv, float depth)
{
    vec4 ndc    = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 worldH = u_InvViewProj * ndc;
    return worldH.xyz / worldH.w;
}

// 从四张图的同一个像素取值，让采到的位置、颜色和法线都来自同一块表面。
bool SampleRSMPoint(vec2 uv, out vec3 flux, out vec3 vplPos, out vec3 normal)
{
    ivec2 texSize = textureSize(u_RSMDepth, 0);

    ivec2 coord = clamp(ivec2(floor(uv * vec2(texSize))), ivec2(0), texSize - 1);

    float d = texelFetch(u_RSMDepth, coord, 0).r;
    if (d >= 0.9999) return false;

    flux   = texelFetch(u_RSMFlux,     coord, 0).rgb;
    vplPos = texelFetch(u_RSMWorldPos, coord, 0).rgb;
    normal = normalize(texelFetch(u_RSMNormal, coord, 0).rgb * 2.0 - 1.0);
    return true;
}

vec3 ComputeRSMIndirect(vec3 fragWorldPos, vec3 N, vec2 shadowUV)
{
    int numSamples = int(u_RSMHeader.x);
    if (numSamples <= 0) return vec3(0.0);

    float intensity    = u_RSMHeader.y;
    float sampleRadius = u_RSMHeader.z;  /* 半径用光源图的 UV 表示，0.1 相当于图宽的十分之一。 */
    float lightWidth = u_RSMHeader.w;
    float texelWidth = lightWidth / float(textureSize(u_RSMDepth, 0).x);

    // 每个像素从不同角度开始采样，每帧再转一点。混合几帧后，固定的采样花纹就会变淡。
    float baseAngle   = Hash12(gl_FragCoord.xy);
    float frameOffset = fract(float(u_FrameIndex) * 0.618033988749895);
    float angle = 6.28318530718 * fract(baseAngle + frameOffset);
    float cosA  = cos(angle);
    float sinA  = sin(angle);

    vec3 indirect = vec3(0.0);

    for (int i = 0; i < numSamples; ++i)
    {
        vec2  baseOffset   = u_RSMSamples[i].xy;
        float sampleWeight = u_RSMSamples[i].z;

        vec2 offset = vec2(
            cosA * baseOffset.x - sinA * baseOffset.y,
            sinA * baseOffset.x + cosA * baseOffset.y);

        vec2 vplUV = shadowUV + offset * sampleRadius;
        if (vplUV.x < 0.0 || vplUV.x > 1.0 ||
            vplUV.y < 0.0 || vplUV.y > 1.0)
            continue;

        vec3 vplFlux, vplPos, vplNormal;
        if (!SampleRSMPoint(vplUV, vplFlux, vplPos, vplNormal))
            continue;

        vec3  dir   = fragWorldPos - vplPos;
        float dist2 = dot(dir, dir);
        if (dist2 == 0.0) continue;

        vec3  dirN       = dir * inversesqrt(dist2);
        float vplFacing  = max(0.0, dot(vplNormal, dirN));
        float fragFacing = max(0.0, dot(N, -dirN));

        // 两块表面越正对彼此，传过去的光越强。距离翻倍时，光降为四分之一。
        // 两点非常近时用一个光源纹素的宽度作下限，防止出现亮斑。
        float geometry = vplFacing * fragFacing / max(dist2, texelWidth * texelWidth);
        indirect += vplFlux * geometry * sampleWeight;
    }

    // 圆盘均匀采样：平均值要乘上采样圆盘在灯光投影平面上的面积。源面和接收面的
    // Lambert 漫反射各含 1/π，故 πr² / π² = r² / π。
    const float PI = 3.14159265;
    float radius = sampleRadius * lightWidth;
    return indirect * (intensity * radius * radius / (PI * float(numSamples)));
}

void main()
{
    float depth = texture(u_SceneDepth, v_TexCoord).r;
    if (depth >= 0.9999)
    {
        o_Indirect = vec4(0.0);
        return;
    }

    vec3 worldPos = ReconstructWorldPos(v_TexCoord, depth);

    // 深度预绘把法线存在相机坐标中，和反光表面的法线比较前先转回世界坐标。
    vec3 viewNormal = texture(u_ViewNormal, v_TexCoord).xyz;
    if (dot(viewNormal, viewNormal) < 0.01)
    {
        o_Indirect = vec4(0.0);
        return;
    }
    mat3 invView3 = transpose(mat3(u_View));
    vec3 N = normalize(invView3 * viewNormal);

    // 同一个接收点还要从灯光看一次，才能定位到反射阴影图上。
    vec4 lsPos = u_LightSpaceMatrix * vec4(worldPos, 1.0);
    vec3 lsProj = lsPos.xyz / max(lsPos.w, 1e-6);
    lsProj = lsProj * 0.5 + 0.5;
    if (lsProj.z < 0.0 || lsProj.z > 1.0)
    {
        o_Indirect = vec4(0.0);
        return;
    }

    vec3 indirect = ComputeRSMIndirect(worldPos, N, lsProj.xy /* 接收点在光源图上的 UV */);
    o_Indirect = vec4(indirect, 1.0);
}
