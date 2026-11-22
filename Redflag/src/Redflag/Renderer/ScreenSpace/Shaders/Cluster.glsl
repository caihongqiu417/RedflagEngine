const uint tile = 16u, slices = 16u;
uvec2 tiles()
{
    return (extent.xy + tile - 1u) / tile;
}
uint words()
{
    return max(1u, (extent.w + 31u) / 32u);
}
float sliceDepth(uint k)
{
    return depthRange.x * pow(depthRange.y / depthRange.x, float(k) / float(slices));
}
uint depthSlice(float z)
{
    float t = log(max(z, depthRange.x) / depthRange.x) / log(depthRange.y / depthRange.x);
    return uint(clamp(floor(t * float(slices)), 0., float(slices - 1u)));
}
uint clusterAt(uvec2 xy, uint k)
{
    return (k * tiles().y + xy.y) * tiles().x + xy.x;
}
