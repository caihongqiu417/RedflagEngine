#type vertex
#version 330 core
layout(location = 0) in vec3 a_Position;
layout(location = 2) in vec2 a_TexCoord;
uniform mat4 u_ViewProjectionModel;
out vec2 v_TexCoord;
void main()
{
    gl_Position = u_ViewProjectionModel * vec4(a_Position, 1.0);
    v_TexCoord = a_TexCoord;
}

#type fragment
#version 330 core
in vec2 v_TexCoord;
uniform sampler2D u_BaseColorMap;
uniform vec4 u_Color;
uniform int u_EntityID;
layout(location = 0) out vec4 o_Color;
layout(location = 1) out int o_EntityID;
void main()
{
    // 面板读颜色，鼠标点选时读同一位置的实体编号。
    o_Color = texture(u_BaseColorMap, v_TexCoord) * u_Color;
    o_EntityID = u_EntityID;
}
