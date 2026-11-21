/*
 * 绘制编辑器中的调试线，例如包围盒边框。
 * 线的位置已经在场景坐标中，只需用相机矩阵换到屏幕上，并保留传入的颜色。
 * 实体编号写为 -1，表示这条调试线不对应可拾取的场景物体。
 */

// ---- 顶点阶段 ----
#type vertex
#version 450 core

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec4 a_Color;

layout(std140, binding = 0) uniform Camera
{
    mat4 u_ViewProjection;
};

layout(location = 0) out vec4 v_Color;

void main()
{
    v_Color     = a_Color;
    gl_Position = u_ViewProjection * vec4(a_Position, 1.0);
}

// ---- 片元阶段 ----
#type fragment
#version 450 core

layout(location = 0) in vec4 v_Color;

layout(location = 0) out vec4  o_Color;
layout(location = 1) out int   o_EntityID;

void main()
{
    o_Color    = v_Color;
    o_EntityID = -1;
}
