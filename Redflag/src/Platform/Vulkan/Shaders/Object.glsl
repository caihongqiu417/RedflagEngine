layout(push_constant) uniform ObjectData {
    mat4 ViewProjectionModel;
    vec4 Color;
#ifdef TRANSPARENT
    float RefractionIndex;
#elif defined(LIGHTING_SURFACES)
    uint MaterialFactors;
#else
    int EntityID;
#endif
    float NormalMatrix[9];
#ifdef TRANSPARENT
    float Thickness;
#else
    float AlphaCutoff;
#endif
    // 着色器逐位读取。1 表示使用法线贴图，2 表示跳过背面，4 表示从贴图读取金属度和粗糙度。
    // 8 标记发光表面。玻璃分两次画，第二次加上这个标记，只画玻璃自己发出的光。
    // 16、32 区分镂空图使用的通道。64 表示按每株或每件物体的矩阵变换顶点。
    uint MaterialFlags;
} u_Object;
