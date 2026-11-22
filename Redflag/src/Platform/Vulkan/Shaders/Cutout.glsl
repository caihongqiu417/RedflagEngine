#ifdef SUBPIXEL_MASK
bool coversArea(sampler2D maskMap, vec2 uv, float mask, int channel)
{
    // 光照图的宽高都是面板的两倍。输出像素在贴图上跨过的宽度翻倍，对应的缩图层级加一。
    vec2 dx = dFdx(uv), dy = dFdy(uv);
    float blend = clamp(textureQueryLod(maskMap, uv).y + 1., 0., 1.);
    if (blend == 0.) return mask >= .5;
    ivec2 sub = ivec2(gl_FragCoord.xy) % 2;
    vec2 across = sub.x == 0 ? dx : -dx;
    vec2 down = sub.y == 0 ? dy : -dy;
    // 同一像素里的四个位置共用一个面积估计，同时各自保留深度、法线和材质。
    // 从左上沿行编号 0 至 3。翻转最低位是左右邻点，翻转第二位是上下邻点。
    int index = sub.y * 2 + sub.x;
    vec4 masks;
    masks[index] = mask;
    masks[index ^ 1] = textureGrad(maskMap, uv + across, dx, dy)[channel];
    masks[index ^ 2] = textureGrad(maskMap, uv + down, dx, dy)[channel];
    masks[index ^ 3] = textureGrad(maskMap, uv + across + down, dx, dy)[channel];
    float area = dot(masks, vec4(.25));

    // 先保留最像实心铁丝的位置，斜着的边也能跟着纹理走。读数相同时才按固定顺序挑选。
    const ivec4 order = ivec4(0, 2, 3, 1);
    int rank = 0;
    for (int i = 0; i < 4; ++i)
        if (masks[i] > mask || (masks[i] == mask && order[i] < order[index])) ++rank;
    // 四档的中点是 1/8、3/8、5/8、7/8。面积最接近四分之一时，只留下排第一的位置。
    float threshold = (float(rank) + .5) / 4.;
    return mix(mask, area, blend) >= mix(.5, threshold, blend);
}
#endif

bool coversSample(sampler2D baseMap, sampler2D opacityMap, vec2 uv, float baseAlpha)
{
    float opacity = texture(opacityMap, uv).r;
#ifdef SUBPIXEL_MASK
    uint source = u_Object.MaterialFlags & 48u; // 16 选底色图的 A，32 选独立透明图的 R。
    if (source == 16u) return coversArea(baseMap, uv, baseAlpha, 3);
    if (source == 32u) return coversArea(opacityMap, uv, opacity, 0);
#endif
    return baseAlpha * u_Object.Color.a * opacity >= u_Object.AlphaCutoff;
}
