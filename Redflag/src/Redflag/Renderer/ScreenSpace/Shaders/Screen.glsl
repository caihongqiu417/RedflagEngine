// 像素从左上角数，表面位置和法线使用世界坐标。投影矩阵保持相机本身的 Y 方向。
struct ScreenSurface
{
    vec4 position, normal, albedo;
};
struct ScreenLight
{
    vec4 position, intensity;
};

layout(std430, binding = 0) readonly buffer Frame
{
    mat4 projection, view, inverseProjection, previousVP, stableVP;
    vec4 eye;
    uvec4 extent;    // 图宽、图高、帧编号、灯数
    vec4 material;   // 遮蔽半径、强度、反射粗糙度、反射距离
    vec4 temporal;   // x 是旧颜色的权重，y 表示上一帧是否可用，z 是漫反射对照测试的统一灯半径，w 表示相机矩阵没变。
    vec4 depthRange; // x、y 用来划分灯簇的深度层。z 非零时按漫反射对照场景计算，所有灯共用 temporal.z 的半径。
};
layout(std430, binding = 1) readonly buffer Geometry
{
    ScreenSurface surfaces[];
};
const float PI = 3.14159265359;
uint indexOf(ivec2 p)
{
    return uint(p.y) * extent.x + uint(p.x);
}
bool onScreen(ivec2 p)
{
    return all(greaterThanEqual(p, ivec2(0))) && all(lessThan(p, ivec2(extent.xy)));
}
vec2 toPixel(vec4 clip)
{
    return (clip.xy / clip.w * vec2(.5, -.5) + .5) * vec2(extent.xy);
}
float depthOf(vec3 p)
{
    return -(view * vec4(p, 1)).z;
}
