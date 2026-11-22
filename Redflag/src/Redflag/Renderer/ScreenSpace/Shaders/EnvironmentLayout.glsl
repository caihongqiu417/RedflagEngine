/* RFIBL01 的纹素顺序：漫反射六面、按粗糙度排列的镜面六面、BRDF 二维表。 */
const int diffuseSize = 32, reflectionSize = 128, reflectionLevels = 5, lutSize = 512;
int reflectionOffset(int level)
{
    int offset = 6 * diffuseSize * diffuseSize;
    for (int mip = 0; mip < level; ++mip)
    {
        int width = reflectionSize >> mip;
        offset += 6 * width * width;
    }
    return offset;
}

// 面的方向和 OpenGL 烘焙时的六台相机一致。uv 从每张图左下角算起。
vec3 cubeDirection(int face, vec2 uv)
{
    vec2 p = 2. * uv - 1.;
    if (face == 0) return vec3(1, -p.y, -p.x);
    if (face == 1) return vec3(-1, -p.y, p.x);
    if (face == 2) return vec3(p.x, 1, p.y);
    if (face == 3) return vec3(p.x, -1, -p.y);
    if (face == 4) return vec3(p.x, -p.y, 1);
    return vec3(-p.x, -p.y, -1);
}
