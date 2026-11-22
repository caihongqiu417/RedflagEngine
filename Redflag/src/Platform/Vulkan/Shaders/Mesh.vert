#version 450
#include "Object.glsl"

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec2 a_TexCoord;
layout(location = 4) in mat4 a_Instance;
layout(location = 0) out vec2 v_TexCoord;
#ifdef EMISSIVE_UV
layout(location = 3) in vec2 a_EmissiveUVOffset;
layout(location = 3) out vec2 v_EmissiveUV;
#endif
#ifdef LIGHTING_SURFACES
layout(location = 2) in vec3 a_Normal;
layout(location = 1) out vec3 v_LocalPosition;
layout(location = 2) out vec3 v_LocalNormal;
#endif

void main()
{
    bool instanced = (u_Object.MaterialFlags & 64u) != 0u;
    vec4 position = vec4(a_Position, 1.0);
    if (instanced) position = a_Instance * position;
    vec4 clip = u_Object.ViewProjectionModel * position;
    // 相机矩阵沿用 OpenGL 的深度范围，Vulkan 的近裁面是 0。
    clip.z = 0.5 * (clip.z + clip.w);
    gl_Position = clip;
    v_TexCoord = a_TexCoord;
#ifdef EMISSIVE_UV
    v_EmissiveUV = a_TexCoord + a_EmissiveUVOffset;
#endif
#ifdef LIGHTING_SURFACES
    v_LocalPosition = position.xyz;
    // 草尖随矩阵倾斜时，表面朝向也随之改变；外层实体变换仍由片元着色器处理。
    v_LocalNormal = instanced ? transpose(inverse(mat3(a_Instance))) * a_Normal : a_Normal;
#endif
}
