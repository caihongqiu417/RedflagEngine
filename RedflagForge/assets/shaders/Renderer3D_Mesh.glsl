// Renderer3D Mesh Shader

#type vertex
#version 450 core

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TexCoord;

// binding = 0: 与 Renderer2D 共用 Camera UBO
layout(std140, binding = 0) uniform Camera
{
    mat4 u_ViewProjection;
};

// binding = 1: 每次 DrawMesh 上传的变换矩阵
layout(std140, binding = 1) uniform Transform
{
    mat4 u_Transform;
};

layout(location = 0) out vec3 v_Normal;
layout(location = 1) out vec2 v_TexCoord;

void main()
{
    v_Normal   = mat3(transpose(inverse(u_Transform))) * a_Normal;
    v_TexCoord = a_TexCoord;
    gl_Position = u_ViewProjection * u_Transform * vec4(a_Position, 1.0);
}

#type fragment
#version 450 core

layout(location = 0) out vec4 o_Color;

layout(location = 0) in vec3 v_Normal;
layout(location = 1) in vec2 v_TexCoord;

// binding = 2: 材质颜色  (.w 分量未使用)
// u_UseTexture 存在 binding=2 的第二个 vec4 的 x 分量
layout(std140, binding = 2) uniform Material
{
    vec4 u_Color;
    vec4 u_Params; // x = UseTexture (0=color, 1=texture)
};

layout(binding = 3) uniform sampler2D u_Texture;

void main()
{
    if (int(u_Params.x) == 1)
        o_Color = texture(u_Texture, v_TexCoord);
    else
        o_Color = u_Color;
}
