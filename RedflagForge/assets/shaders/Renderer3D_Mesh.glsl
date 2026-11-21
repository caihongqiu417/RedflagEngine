/* 绘制模型表面。先算灯光、阴影和反射，再把颜色交给后处理。
 * 同时写出实体编号，供编辑器拾取模型。 */

// ---- 顶点阶段 ----
#type vertex
#version 450 core

// ---- 网格顶点 ----
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TexCoord;
layout(location = 3) in vec3 a_Tangent;

// ---- 相机与阴影 ----
layout(std140, binding = 0) uniform Camera
{
    mat4 u_ViewProjection;
    vec4 u_ViewPosition;
    mat4 u_LightSpaceMatrix;
    vec4 u_ShadowParams;
    vec4 u_ShadowParams2;
    mat4 u_View;
};

// 合批绘制时，每个实例带自己的模型矩阵、颜色和实体编号。
#ifdef INSTANCED
layout(location = 4) in mat4 a_Transform;
layout(location = 8) in vec4 a_BaseColor;
layout(location = 9) in vec4 a_MatParams;   // 金属度、粗糙度、环境遮蔽
layout(location = 10) in int a_EntityID;
#else
layout(std140, binding = 1) uniform Transform
{
    mat4 u_Transform;
};
#endif

// ---- 顶点传给片元的数据 ----
layout(location = 0) out vec3  v_WorldPos;
layout(location = 1) out vec2  v_TexCoord;
layout(location = 2) out vec3  v_Normal;
layout(location = 3) out mat3  v_TBN;  // 将法线贴图中的方向转到世界空间
layout(location = 6) out vec4  v_LightSpacePos;  // 从方向光看这个点的位置，保留 w 供片元计算阴影

#ifdef INSTANCED
layout(location = 7) flat out vec4 v_BaseColor;
layout(location = 8) flat out vec4 v_MatParams;
layout(location = 9) flat out int v_EntityID;
#endif

void main()
{
#ifdef INSTANCED
    mat4 model = a_Transform;
    v_BaseColor = a_BaseColor;
    v_MatParams = a_MatParams;
    v_EntityID = a_EntityID;
#else
    mat4 model = u_Transform;
#endif
    vec4 worldPos = model * vec4(a_Position, 1.0);
    v_WorldPos = worldPos.xyz;
    v_TexCoord = a_TexCoord;

    // 切线沿表面伸缩。法线用逆转置变换，才能在非均匀缩放后仍垂直于表面。
    mat3 axes = mat3(model);
    vec3 N = normalize(transpose(inverse(axes)) * a_Normal);
    vec3 T = axes * a_Tangent;
    T = normalize(T - dot(T, N) * N);
    v_Normal = N;
    v_TBN = mat3(T, cross(N, T), N);

    v_LightSpacePos = u_LightSpaceMatrix * worldPos;
    gl_Position = u_ViewProjection * worldPos;
}

// ---- 片元阶段 ----
#type fragment
#version 450 core

layout(location = 0) out vec4 o_Color;
layout(location = 1) out int  o_EntityID;  // 鼠标指向这个像素时读到的实体编号

layout(location = 0) in vec3  v_WorldPos;
layout(location = 1) in vec2  v_TexCoord;
layout(location = 2) in vec3  v_Normal;
layout(location = 3) in mat3  v_TBN;
layout(location = 6) in vec4  v_LightSpacePos;
#ifdef INSTANCED
layout(location = 7) flat in vec4 v_BaseColor;
layout(location = 8) flat in vec4 v_MatParams;
layout(location = 9) flat in int v_EntityID;
#endif

// ---- 相机与阴影 ----
layout(std140, binding = 0) uniform Camera
{
    mat4 u_ViewProjection;
    vec4 u_ViewPosition;  // xyz 是相机位置，w 为 1 时使用环境贴图
    mat4 u_LightSpaceMatrix;
    vec4 u_ShadowParams;  // x、y 控制深度偏移，z 是阴影图宽度的倒数，w 选择阴影模式
    vec4 u_ShadowParams2;  // x 是滤波半径，y 是软阴影光源大小，两者以阴影图像素计。zw 是视口宽高。
    mat4 u_View;
};

// ---- 材质 ----
layout(std140, binding = 2) uniform Material
{
    vec4  u_BaseColor;  // rgb 给底色贴图着色，a 参与透明裁剪
    float u_Metallic;  // 0 为非金属，1 为纯金属
    float u_Roughness;  // 越光滑，高光越集中
    float u_AO;  // 留给环境光的比例，1 表示完全保留
    int   u_UseBaseColorMap;
    int   u_UseNormalMap;
    int   u_UseMetallicRoughnessMap;
    int   u_UseAOMap;
    int   u_EntityID;
    int   u_UseAlphaMap;
    float u_AlphaCutoff;  // Mask 材质中，小于此透明度的像素会被丢弃
    int   u_AlphaMode;  // 0 不透明，1 按阈值裁剪，2 输出混合用的透明度
    int   _matPad0;
};

// ---- 场景灯光 ----
struct LightData
{
    vec4 Direction;  // xyz 是光线照射的方向
    vec4 Color;  // rgb 是光的颜色，w 是强度
    vec4 Position;  // 点光源的世界位置，w 是它能照到的半径
    int  CastShadow;
    int  _pad0;
    int  _pad1;
    int  _pad2;
};

layout(std140, binding = 3) uniform Lights
{
    int       u_NumDirectionalLights;
    int       u_NumPointLights;
    int       _lightPad0;
    int       _lightPad1;
    LightData u_DirLights[1];
    LightData u_PointLights[16];
};

// ---- 材质贴图 ----
layout(binding = 3) uniform sampler2D u_BaseColorMap;
layout(binding = 4) uniform sampler2D u_NormalMap;
layout(binding = 5) uniform sampler2D u_MetallicRoughnessMap;  // G 为粗糙度，B 为金属度
layout(binding = 6) uniform sampler2D u_AOMap;  // R 为环境光保留的比例
layout(binding = 10) uniform sampler2D u_AlphaMap;  // R 为透明裁剪和混合用的 alpha

// ---- 阴影与环境光 ----
layout(binding = 11) uniform sampler2D u_ShadowMap;  // 从灯光看，最近表面的深度
layout(binding = 12) uniform sampler2D u_SSAOMap;  // 屏幕上的环境遮蔽，关闭时读到白色

// 环境贴图加载后，漫反射颜色、模糊反射和材质反射系数分别从这三张图读取。
layout(binding = 7) uniform samplerCube u_IrradianceMap;  // 按表面法线查漫反射光
layout(binding = 8) uniform samplerCube u_PrefilterMap;  // 按反射方向查环境颜色，粗糙的表面取更模糊的一层
layout(binding = 9) uniform sampler2D   u_BRDFLUT;  // 用观察角度和粗糙度查镜面反射的两个系数

layout(binding = 17) uniform sampler2D u_RSMResolved;  // 白色漫反射表面收到的一次反射光，取多帧累积后的结果

// ---- 表面反射 ----
// Cook–Torrance 镜面项 = D × G × F / (4 × (N·V) × (N·L))。
// D 决定高光的形状，G 估计细小表面之间的遮挡，F 决定有多少光被反射。

const float PI = 3.14159265359;

// GGX 描述细小表面法线的分布，H 是让光线恰好反射到相机的法线方向。
// α = roughness²，D = α² / [π ((N·H)² (α² − 1) + 1)²]。
float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a  = roughness * roughness;
    float a2 = a * a;
    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    return a2 / max(denom, 0.0001);
}

// Schlick-GGX 估计从一个方向看去，还有多少细小表面可见。
float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;  // 这里的 k 用于直接光照
    return NdotV / (NdotV * (1.0 - k) + k);
}

// 光要能照到这些表面，相机也要能看到它们。因此两个方向的可见比例相乘。
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

// 菲涅尔近似。正对微小表面时反射率为 F0，贴着表面看时反射变强。
vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// 环境反射使用粗糙度修正，避免粗糙表面的边缘亮得像镜面。
vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0)
             * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// ---- 方向光阴影 ----
// 阴影图保存灯光最近看到的表面。当前点比它更远，就被挡住了。
float SampleShadow(vec2 uv, float refDepth)
{
    float occ = texture(u_ShadowMap, uv).r;
    return refDepth <= occ ? 1.0 : 0.0;
}

// 比较周围 3×3 个位置。能看到光源的比例决定阴影边缘的明暗。
float PCF3x3(vec2 baseUV, float refDepth, float texel)
{
    float sum = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            sum += SampleShadow(baseUV + vec2(x, y) * texel, refDepth);
    return sum / 9.0;
}

// 仍然只比较 5×5 个位置。半径越大，采样点越分散，阴影边缘越宽。
float PCFVariable(vec2 baseUV, float refDepth, float radius, float texel)
{
    float sum = 0.0;
    float samples = 0.0;
    for (int y = -2; y <= 2; ++y)
    {
        for (int x = -2; x <= 2; ++x)
        {
            vec2 offset = vec2(x, y) * (radius * 0.5) * texel;
            sum     += SampleShadow(baseUV + offset, refDepth);
            samples += 1.0;
        }
    }
    return sum / samples;
}

// 计算挡在接收点前面的那些表面的平均深度。找不到遮挡时返回 -1。
float FindBlockerDepth(vec2 baseUV, float refDepth, float searchRadius, float texel)
{
    float blockerSum = 0.0;
    float blockers   = 0.0;
    const int N = 2;
    for (int y = -N; y <= N; ++y)
    {
        for (int x = -N; x <= N; ++x)
        {
            vec2 offset = vec2(x, y) * (searchRadius / float(N)) * texel;
            float d = texture(u_ShadowMap, baseUV + offset).r;
            if (d < refDepth)
            {
                blockerSum += d;
                blockers   += 1.0;
            }
        }
    }
    if (blockers < 1.0) return -1.0;
    return blockerSum / blockers;
}

// 返回当前点能看到多少方向光，0 表示完全被遮挡。
float ComputeDirectionalShadow(vec4 lightSpacePos, vec3 N, vec3 L)
{
    int mode = int(u_ShadowParams.w);
    if (mode == 0) return 1.0;

    // 除以 w，再把坐标从 [-1,1] 映射到阴影图的 UV 和深度范围 [0,1]。
    vec3 proj = lightSpacePos.xyz / lightSpacePos.w;
    proj = proj * 0.5 + 0.5;

    // 阴影图只记录了灯光相机覆盖的范围。范围外按受光处理。
    if (proj.z > 1.0 || proj.z < 0.0)                return 1.0;
    if (any(lessThan(proj.xy, vec2(0.0))) ||
        any(greaterThan(proj.xy, vec2(1.0))))         return 1.0;

    float texel      = u_ShadowParams.z;  // 阴影图上一个像素的 UV 宽度
    float depthBias  = u_ShadowParams.x;
    float normalBias = u_ShadowParams.y;

    // 斜面容易把自己判为遮挡物，深度比较时让它稍微靠近灯光一些。
    float NdotL = max(dot(N, L), 0.0);
    float bias  = max(depthBias * (1.0 - NdotL), depthBias * 0.1);
    bias       += normalBias * texel;  // 额外偏移随阴影图分辨率缩放
    float refDepth = proj.z - bias;

    if (mode == 2)
    {
        // PCSS 先找遮挡物，再根据遮挡物与接收面的距离估算阴影边缘的宽度。
        float LIGHT_SIZE   = max(u_ShadowParams2.y, 0.0);
        const float SEARCH_RADIUS = 4.0;

        float avgBlocker = FindBlockerDepth(proj.xy, refDepth, SEARCH_RADIUS /* 向四周各找 4 个阴影像素 */, texel);
        if (avgBlocker < 0.0) return 1.0;

        // 两者越贴近，影子越清晰。
        float penumbra = (refDepth - avgBlocker) / max(avgBlocker, 0.0001);
        float radius   = clamp(penumbra * LIGHT_SIZE, 0.5, 16.0);
        return PCFVariable(proj.xy, refDepth, radius /* 半影半径，按阴影图像素计 */, texel);
    }

    // 固定半径的 PCF。半径在一个阴影像素以内用 3×3，较大时用 5×5。
    float pcfRadius = u_ShadowParams2.x;
    if (pcfRadius <= 1.001)
        return PCF3x3(proj.xy, refDepth, texel);
    return PCFVariable(proj.xy, refDepth, pcfRadius, texel);
}

// ---- 直接光照 ----
// 方向光照在各处的方向相同，亮度也不随距离改变。
vec3 CalcDirectionalLight(LightData light, vec3 N, vec3 V, vec3 albedo,
                          float metallic, float roughness, vec3 F0, float shadow)
{
    // 灯的 Direction 沿光线照射方向，算光照时 L 从表面指向灯。
    vec3 L = normalize(-light.Direction.xyz);
    vec3 H = normalize(V + L);
    vec3 radiance = light.Color.xyz * light.Color.w;

    float NDF = DistributionGGX(N, H, roughness);
    float G   = GeometrySmith(N, V, L, roughness);
    vec3  F   = FresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3  numerator   = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3  specular    = numerator / denominator;

    // 很光滑的表面会产生尖锐的高光，限制峰值以免亮成一片。
    specular = min(specular, vec3(80.0));

    // 为漫反射留下镜面反射用剩的部分。纯金属只产生镜面反射。
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

    float NdotL = max(dot(N, L), 0.0);
    return (kD * albedo / PI + specular) * radiance * NdotL * shadow;
}

// 点光源只在给定半径内照亮场景，表面和灯的位置都按世界坐标计算。
vec3 CalcPointLight(LightData light, vec3 N, vec3 V, vec3 worldPos,
                    vec3 albedo, float metallic, float roughness, vec3 F0)
{
    vec3  lightPos = light.Position.xyz;
    float radius   = light.Position.w;
    vec3  L        = lightPos - worldPos;
    float distance = length(L);

    if (distance > radius)
        return vec3(0.0);

    L = normalize(L);
    vec3 H = normalize(V + L);

    // (1 − (d/r)²)² 让灯光在半径边缘平滑地减弱到零。
    float t = clamp(1.0 - (distance * distance) / (radius * radius), 0.0, 1.0);
    float attenuation = t * t;

    vec3 radiance = light.Color.xyz * light.Color.w * attenuation;

    // 点光和方向光共用同一套表面反射公式。
    float NDF = DistributionGGX(N, H, roughness);
    float G   = GeometrySmith(N, V, L, roughness);
    vec3  F   = FresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3  numerator   = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3  specular    = numerator / denominator;
    specular = min(specular, vec3(80.0));

    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

    float NdotL = max(dot(N, L), 0.0);
    return (kD * albedo / PI + specular) * radiance * NdotL;
}

// ---- 模型表面着色 ----
void main()
{
    // ---- 材质 ----
#ifdef INSTANCED
    vec4 albedo4 = v_BaseColor;
    float metallic = v_MatParams.x;
    float roughness = v_MatParams.y;
    float ao = v_MatParams.z;
    int entityID = v_EntityID;
#else
    vec4 albedo4 = u_BaseColor;
    float metallic = u_Metallic;
    float roughness = u_Roughness;
    float ao = u_AO;
    int entityID = u_EntityID;
#endif
    if (u_UseBaseColorMap == 1)
    {
        vec4 texColor = texture(u_BaseColorMap, v_TexCoord);
        // 用 2.2 次幂近似把底色转成线性 RGB，之后才能和光照相乘。
        texColor.rgb = pow(texColor.rgb, vec3(2.2));
        albedo4 *= texColor;
    }
    vec3 albedo = albedo4.rgb;

    // Mask 的轮廓同时受材质颜色、底色贴图和独立透明贴图影响。
    float alphaValue = albedo4.a;
    if (u_UseAlphaMap == 1)
        alphaValue *= texture(u_AlphaMap, v_TexCoord).r;
    if (u_AlphaMode == 1 && alphaValue < u_AlphaCutoff)
        discard;

    // 只有半透明材质需要将 alpha 留给后续混合。
    float outAlpha = (u_AlphaMode == 2) ? alphaValue : 1.0;

    if (u_UseMetallicRoughnessMap == 1)
    {
        vec4 mrSample = texture(u_MetallicRoughnessMap, v_TexCoord);
        roughness *= mrSample.g;
        metallic  *= mrSample.b;
    }
    // 给最光滑的材质保留一点粗糙度，避免高光过于集中。
    roughness = clamp(roughness, 0.089, 1.0);

    if (u_UseAOMap == 1)
        ao *= texture(u_AOMap, v_TexCoord).r;

    // SSAO 描述屏幕上这个位置有多遮蔽，要用屏幕坐标查，材质图才用模型的 UV。
    vec2 screenUV = gl_FragCoord.xy / max(u_ShadowParams2.zw, vec2(1.0));
    float ssao = texture(u_SSAOMap, screenUV).r;
    ao *= ssao;

    // ---- 表面朝向 ----
    vec3 N;
    if (u_UseNormalMap == 1)
    {
        // 贴图把三个方向分量存在 [0,1]，先还原成 [-1,1]，再转到世界空间。
        vec3 tangentNormal = texture(u_NormalMap, v_TexCoord).rgb * 2.0 - 1.0;
        N = normalize(v_TBN * tangentNormal);
    }
    else
    {
        N = normalize(v_Normal);
    }

    vec3 V = normalize(u_ViewPosition.xyz - v_WorldPos);

    vec3 F0 = mix(vec3(0.04) /* 非金属正对光线时的反射率 */, albedo /* 纯金属的反射颜色 */, metallic);

    // ---- 灯光与阴影 ----
    vec3 Lo = vec3(0.0);

    // 阴影图来自第一盏方向光。点光源只计算距离衰减。
    for (int i = 0; i < u_NumDirectionalLights; i++)
    {
        vec3 L = normalize(-u_DirLights[i].Direction.xyz);
        float shadow = (i == 0)
            ? ComputeDirectionalShadow(v_LightSpacePos, N, L)
            : 1.0;
        Lo += CalcDirectionalLight(u_DirLights[i], N, V, albedo, metallic, roughness, F0, shadow);
    }

    for (int i = 0; i < u_NumPointLights; i++)
        Lo += CalcPointLight(u_PointLights[i], N, V, v_WorldPos, albedo, metallic, roughness, F0);

    // 场景里还没有灯时，用一盏固定的白光方便查看模型。
    if (u_NumDirectionalLights == 0 && u_NumPointLights == 0)
    {
        LightData fallback;
        fallback.Direction = vec4(0.0, -1.0, -0.5, 0.0);
        fallback.Color     = vec4(1.0, 1.0, 1.0, 1.0);
        Lo += CalcDirectionalLight(fallback, N, V, albedo, metallic, roughness, F0, 1.0 /* 默认灯光没有阴影 */);
    }

    // 用表面的漫反射颜色给 RSM 反射光着色。纯金属没有漫反射。
    Lo += texture(u_RSMResolved, screenUV).rgb * albedo * (1.0 - metallic);

    // ---- 环境光 ----
    vec3 ambient;

    if (u_ViewPosition.w > 0.5)
    {
        vec3 kS = FresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
        vec3 kD = (1.0 - kS) * (1.0 - metallic);

        // 漫反射按表面朝向取周围的光。
        vec3 irradiance = texture(u_IrradianceMap, N).rgb;
        vec3 diffuseIBL = irradiance * albedo;

        // 镜面反射沿视线的反射方向取环境颜色。粗糙度越大，反射越模糊。
        vec3 R = reflect(-V, N);

        const float MAX_REFLECTION_LOD = 4.0;
        vec3 prefilteredColor = textureLod(u_PrefilterMap, R, roughness * MAX_REFLECTION_LOD /* 0 最清晰，4 最模糊 */).rgb;

        // 查表得到两个系数，决定这个观察角度下要保留多少环境反射。
        vec2 brdf = texture(u_BRDFLUT, vec2(max(dot(N, V), 0.0), roughness)).rg;
        vec3 specularIBL = prefilteredColor * (kS * brdf.x + brdf.y);

        ambient = (kD * diffuseIBL + specularIBL) * ao;
    }
    else
    {
        // 没有环境贴图时，给背光面留一点底色。
        ambient = vec3(0.03) * albedo * ao;
    }

    vec3 color = ambient + Lo;

    // 保留高于 1 的亮度，后处理会用这些亮部生成光晕，再映射到屏幕颜色。
    o_Color    = vec4(color, outAlpha);
    o_EntityID = entityID;
}
