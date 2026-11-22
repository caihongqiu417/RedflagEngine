#version 450
#include "Object.glsl"
#define SUBPIXEL_MASK
#include "Cutout.glsl"

layout(location = 0) in vec2 v_TexCoord;
layout(location = 1) in vec3 v_LocalPosition;
layout(location = 2) in vec3 v_LocalNormal;
layout(location = 4) in vec3 v_ViewLocal;
#include "HeightSurface.glsl"
layout(location = 3) in vec2 v_EmissiveUV;
layout(set = 0, binding = 0) uniform sampler2D baseColorMap;
layout(set = 1, binding = 0) uniform sampler2D emissiveMap;
layout(set = 2, binding = 0) uniform sampler2D opacityMap;
layout(location = 0) out vec4 emission;

void main()
{
    // 灯牌上的透明孔应露出后面的物体，裁剪条件与绘制底色和深度时相同。
    if (u_Object.AlphaCutoff >= 0. && !coversSample(baseColorMap, opacityMap, v_TexCoord, texture(baseColorMap, v_TexCoord).a)) discard;
    bool back = (u_Object.MaterialFlags & 2u) != 0u && !gl_FrontFacing;
    emission = vec4(0., 0., 0., 1.);
    if (!back && any(greaterThan(u_Object.Color.rgb, vec3(0.))))
        {
        vec2 uv = reliefUV(opacityMap, v_TexCoord, v_LocalPosition, v_LocalNormal, v_ViewLocal);
        emission.rgb = u_Object.Color.rgb * texture(emissiveMap, v_EmissiveUV + uv - v_TexCoord).rgb;
    }
}
