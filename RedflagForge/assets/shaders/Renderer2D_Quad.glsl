/* 将同一批精灵画出来，每个精灵可以使用不同的贴图、颜色和平铺次数。 */

#type vertex
#version 450 core

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec4 a_Color;
layout(location = 2) in vec2 a_TexCoord;
layout(location = 3) in float a_TexIndex;
layout(location = 4) in float a_TilingFactor;  /* 每个方向重复贴图的次数，1 表示铺满一次 */
layout(location = 5) in int a_EntityID;

// 这一批精灵使用同一台相机。
layout(std140, binding = 0) uniform Camera
{
    mat4 u_ViewProjection;
};

layout(location = 0) out vec4 v_Color;
layout(location = 1) out vec2 v_TexCoord;
layout(location = 2) out flat float v_TexIndex;
layout(location = 3) out float v_TilingFactor;
layout(location = 4) out flat int v_EntityID;

void main()
{
    v_Color = a_Color;
    v_TexCoord = a_TexCoord;
    v_TexIndex = a_TexIndex;
    v_TilingFactor = a_TilingFactor;
    v_EntityID = a_EntityID;
    
    gl_Position = u_ViewProjection * vec4(a_Position, 1.0);
}

#type fragment
#version 450 core

layout(location = 0) out vec4 color;
layout(location = 1) out int color2;

layout(location = 0) in vec4 v_Color;
layout(location = 1) in vec2 v_TexCoord;
layout(location = 2) in flat float v_TexIndex;
layout(location = 3) in float v_TilingFactor;
layout(location = 4) in flat int v_EntityID;

// 每个精灵用自己的 v_TexIndex 从这一批贴图中选一张。
uniform sampler2D u_Textures[32];

void main()
{
    vec4 texColor = v_Color;
    switch(int(v_TexIndex))
    {
        case 0: texColor *= texture(u_Textures[0], v_TexCoord * v_TilingFactor); break;
        case 1: texColor *= texture(u_Textures[1], v_TexCoord * v_TilingFactor); break;
        case 2: texColor *= texture(u_Textures[2], v_TexCoord * v_TilingFactor); break;
        case 3: texColor *= texture(u_Textures[3], v_TexCoord * v_TilingFactor); break;
        case 4: texColor *= texture(u_Textures[4], v_TexCoord * v_TilingFactor); break;
        case 5: texColor *= texture(u_Textures[5], v_TexCoord * v_TilingFactor); break;
        case 6: texColor *= texture(u_Textures[6], v_TexCoord * v_TilingFactor); break;
        case 7: texColor *= texture(u_Textures[7], v_TexCoord * v_TilingFactor); break;
        case 8: texColor *= texture(u_Textures[8], v_TexCoord * v_TilingFactor); break;
        case 9: texColor *= texture(u_Textures[9], v_TexCoord * v_TilingFactor); break;
        case 10: texColor *= texture(u_Textures[10], v_TexCoord * v_TilingFactor); break;
        case 11: texColor *= texture(u_Textures[11], v_TexCoord * v_TilingFactor); break;
        case 12: texColor *= texture(u_Textures[12], v_TexCoord * v_TilingFactor); break;
        case 13: texColor *= texture(u_Textures[13], v_TexCoord * v_TilingFactor); break;
        case 14: texColor *= texture(u_Textures[14], v_TexCoord * v_TilingFactor); break;
        case 15: texColor *= texture(u_Textures[15], v_TexCoord * v_TilingFactor); break;
        case 16: texColor *= texture(u_Textures[16], v_TexCoord * v_TilingFactor); break;
        case 17: texColor *= texture(u_Textures[17], v_TexCoord * v_TilingFactor); break;
        case 18: texColor *= texture(u_Textures[18], v_TexCoord * v_TilingFactor); break;
        case 19: texColor *= texture(u_Textures[19], v_TexCoord * v_TilingFactor); break;
        case 20: texColor *= texture(u_Textures[20], v_TexCoord * v_TilingFactor); break;
        case 21: texColor *= texture(u_Textures[21], v_TexCoord * v_TilingFactor); break;
        case 22: texColor *= texture(u_Textures[22], v_TexCoord * v_TilingFactor); break;
        case 23: texColor *= texture(u_Textures[23], v_TexCoord * v_TilingFactor); break;
        case 24: texColor *= texture(u_Textures[24], v_TexCoord * v_TilingFactor); break;
        case 25: texColor *= texture(u_Textures[25], v_TexCoord * v_TilingFactor); break;
        case 26: texColor *= texture(u_Textures[26], v_TexCoord * v_TilingFactor); break;
        case 27: texColor *= texture(u_Textures[27], v_TexCoord * v_TilingFactor); break;
        case 28: texColor *= texture(u_Textures[28], v_TexCoord * v_TilingFactor); break;
        case 29: texColor *= texture(u_Textures[29], v_TexCoord * v_TilingFactor); break;
        case 30: texColor *= texture(u_Textures[30], v_TexCoord * v_TilingFactor); break;
        case 31: texColor *= texture(u_Textures[31], v_TexCoord * v_TilingFactor); break;
    }

    // 转成线性颜色，最后由后处理统一校正屏幕亮度。
    texColor.rgb = pow(texColor.rgb, vec3(2.2));
    color = texColor;

    // 丢弃接近全透明的片段，避免透明像素写入深度缓冲遮挡后续内容
    if (color.a < 0.01)
        discard;

    color2 = v_EntityID;
}