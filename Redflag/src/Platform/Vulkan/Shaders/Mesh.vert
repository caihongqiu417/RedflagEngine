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
layout(location = 2) in vec3 a_Normal;
layout(set = 2, binding = 0) uniform sampler2D heightMap;
layout(location = 4) out vec3 v_ViewLocal;

layout(location = 1) out vec3 v_LocalPosition;
layout(location = 2) out vec3 v_LocalNormal;

void main()
{
    bool instanced = (u_Object.MaterialFlags & 64u) != 0u;
    vec4 position = vec4(a_Position, 1.0);
    if (heightMode() == 3u)
    {
        // 白色零深度在原表面，黑色谷底向模型内部移动 HeightScale 米。
        float depth = 1.0 - textureLod(heightMap, a_TexCoord, 0.0).r;
        position.xyz -= a_Normal * (heightRange() * depth);
    }
    if (instanced) position = a_Instance * position;
    v_ViewLocal = vec3(0);
    if (heightMode() == 2u)
    {
        // 透视相机返回眼睛的位置，正交相机返回平行视线的方向。
        vec4 eye = inverse(u_Object.ViewProjectionModel) * vec4(0, 0, -1, 0);
        v_ViewLocal = eye.xyz - eye.w * position.xyz;
    }
    vec4 clip = u_Object.ViewProjectionModel * position;
    // 相机矩阵沿用 OpenGL 的深度范围，Vulkan 的近裁面是 0。
    clip.z = 0.5 * (clip.z + clip.w);
    gl_Position = clip;
    v_TexCoord = a_TexCoord;
#ifdef EMISSIVE_UV
    v_EmissiveUV = a_TexCoord + a_EmissiveUVOffset;
#endif
    // 法线贴图和高度梯度以原始 UV 表面为基准，深度仍由上面的置换后顶点决定。
    v_LocalPosition = heightMode() == 3u ? (instanced ? (a_Instance * vec4(a_Position, 1)).xyz : a_Position) : position.xyz;
    // 草尖随矩阵倾斜时，表面朝向也随之改变；外层实体变换仍由片元着色器处理。
    v_LocalNormal = instanced ? transpose(inverse(mat3(a_Instance))) * a_Normal : a_Normal;
}
