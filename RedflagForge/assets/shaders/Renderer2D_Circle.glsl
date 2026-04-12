// Renderer2D Circle Shader

#type vertex
#version 450 core

// ========== �������� ==========
layout(location = 0) in vec3 a_WorldPosition;   // �������꣨�Ѿ��� CPU �任��
layout(location = 1) in vec3 a_LocalPosition;   // �ֲ����꣨-1.0 ~ 1.0��Բ���� 0��
layout(location = 2) in float a_Thickness;      // Բ�����
layout(location = 3) in vec4 a_Color;           // ��ɫ
layout(location = 4) in float a_Fade;           // ��Ե��
layout(location = 5) in int a_EntityID;         // ʵ�� ID

// ========== Uniform Buffer ==========
layout(std140, binding = 0) uniform Camera
{
    mat4 u_ViewProjection;
};

// ========== ���ݸ�Ƭ����ɫ�� ==========
layout(location = 0) out vec4 v_Color;
layout(location = 1) out vec2 v_LocalPos;       // ��ֵ��ľֲ�����
layout(location = 2) out float v_Thickness;
layout(location = 3) out float v_Fade;
layout(location = 4) flat out int v_EntityID;

void main()
{
    v_Color = a_Color;
    v_LocalPos = a_LocalPosition.xy;    // ���ݾֲ����꣬Ƭ����ɫ����������
    v_Thickness = a_Thickness;
    v_Fade = a_Fade;
    v_EntityID = a_EntityID;
    
    // ��������ֱ�����ڶ��㶨λ
    gl_Position = u_ViewProjection * vec4(a_WorldPosition, 1.0);
}


#type fragment
#version 450 core

// ========== ��� ==========
layout(location = 0) out vec4 o_Color;
layout(location = 1) out int o_EntityID;

// ========== �Ӷ�����ɫ������ ==========
layout(location = 0) in vec4 v_Color;
layout(location = 1) in vec2 v_LocalPos;
layout(location = 2) in float v_Thickness;
layout(location = 3) in float v_Fade;
layout(location = 4) flat in int v_EntityID;

void main()
{
    // 1. �������ص�Բ�ĵľ��루Բ���� 0,0��
    float dist = length(v_LocalPos);
    float radius = 1.0;   // �ֲ����귶Χ -1~1���뾶 = 1
    
    // 2. ��������߽�
    float inner = radius - v_Thickness;   // �ڰ뾶��ʵ��Բʱ < 0��
    float outer = radius;                 // ��뾶
    
    // 3. ���Ե�𻯣��� outer - fade �� outer ����͸��
    float alphaOuter = 1.0 - smoothstep(outer - v_Fade, outer, dist);
    
    // 4. �ڱ�Ե�𻯣��� inner - fade �� inner ����͸��������Բ��Ч����
    float alphaInner = smoothstep(inner - v_Fade, inner, dist);
    
    // 5. ���� alpha
    float alpha = alphaOuter * alphaInner;
    
    // 6. ������ <= 0����Ⱦʵ��Բ�����ڱ�Ե��
    if (v_Thickness <= 0.0)
        alpha = alphaOuter;
    
    // 7. �����ɫ
    o_Color = v_Color;
    o_Color.a *= alpha;
    o_EntityID = v_EntityID;
    
    // 8. ��ȫ͸����Ƭ�ζ���
    if (o_Color.a < 0.01)
        discard;
}