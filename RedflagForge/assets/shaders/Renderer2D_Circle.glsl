/* 在四边形上画圆：根据像素到圆心的距离，留下圆盘或圆环。 */

#type vertex
#version 450 core

layout(location = 0) in vec3 a_WorldPosition;
layout(location = 1) in vec3 a_LocalPosition;  /* 圆心为 (0,0)，圆边到圆心的距离为 1 */
layout(location = 2) in float a_Thickness;  /* 环宽，以圆半径为 1 */
layout(location = 3) in vec4 a_Color;
layout(location = 4) in float a_Fade;  /* 边缘渐隐的宽度，单位与环宽相同 */
layout(location = 5) in int a_EntityID;

// ========== Uniform Buffer ==========
layout(std140, binding = 0) uniform Camera
{
    mat4 u_ViewProjection;
};

layout(location = 0) out vec4 v_Color;
layout(location = 1) out vec2 v_LocalPos;
layout(location = 2) out float v_Thickness;
layout(location = 3) out float v_Fade;
layout(location = 4) flat out int v_EntityID;

void main()
{
    v_Color = a_Color;
    v_LocalPos = a_LocalPosition.xy;
    v_Thickness = a_Thickness;
    v_Fade = a_Fade;
    v_EntityID = a_EntityID;
    
    gl_Position = u_ViewProjection * vec4(a_WorldPosition, 1.0);
}


#type fragment
#version 450 core

layout(location = 0) out vec4 o_Color;
layout(location = 1) out int o_EntityID;

layout(location = 0) in vec4 v_Color;
layout(location = 1) in vec2 v_LocalPos;
layout(location = 2) in float v_Thickness;
layout(location = 3) in float v_Fade;
layout(location = 4) flat in int v_EntityID;

void main()
{
    float dist = length(v_LocalPos);
    float radius = 1.0;
    
    float inner = radius - v_Thickness;
    float outer = radius;
    
    float alphaOuter = 1.0 - smoothstep(outer - v_Fade /* 从这里开始变淡 */, outer /* 圆边 */, dist);
    
    float alphaInner = smoothstep(inner - v_Fade, inner, dist);
    
    float alpha = alphaOuter * alphaInner;
    
    // 环宽为 0 时画实心圆，只让外沿变淡。
    if (v_Thickness <= 0.0)
        alpha = alphaOuter;
    
    o_Color = v_Color;
    // 圆和精灵都先写线性颜色，最后一起做显示校正。
    o_Color.rgb = pow(o_Color.rgb, vec3(2.2));
    o_Color.a *= alpha;
    o_EntityID = v_EntityID;
    
    // 圆外的透明像素也不该被鼠标点中。
    if (o_Color.a < 0.01)
        discard;
}