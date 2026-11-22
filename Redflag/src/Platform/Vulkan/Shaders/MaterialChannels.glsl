// 金属度和粗糙度分别用 12 位表示，合成一个整数，写进底色图的 A 通道。
// 加 1 把背景值 0 留出来，两项材质值全为零的网格也能被认出。最大值 2^24 仍能用 float 精确保存。
float encodeMaterial(vec2 metallicRoughness)
{
    uvec2 q = uvec2(round(clamp(metallicRoughness, 0., 1.) * 4095.));
    return float(1u + (q.x << 12u) + q.y);
}

vec2 decodeMaterial(float encoded)
{
    uint q = uint(encoded) - 1u;
    return vec2(q >> 12u, q & 4095u) / 4095.;
}
