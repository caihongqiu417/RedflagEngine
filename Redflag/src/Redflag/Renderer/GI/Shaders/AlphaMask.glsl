struct Mask { vec4 parameters; uvec4 opacity; };
layout(std430, binding = RF_MASK_BINDING) readonly buffer AlphaMasks { Mask alphaMasks[]; };

float maskTexel(uvec3 image, ivec2 pixel, uint bytesPerPixel, uint channel)
{
    ivec2 size = ivec2(image.yz);
    pixel = (pixel + size) % size;
    uint pixelIndex = uint(pixel.y) * image.y + uint(pixel.x);
    uint byteIndex = pixelIndex * bytesPerPixel + channel;
    // 一个 uint 存四个字节。先找到这个像素所在的 uint，再取出需要的那一个字节。
    uint word = texels[image.x + byteIndex / 4u];
    return float((word >> (8u * (byteIndex % 4u))) & 255u) / 255.;
}

float sampleMask(uvec4 image, vec2 uv, uint channel)
{
    if (image.y == 0u) return 1.;
    uint bytesPerPixel = 4u;
    if (image.w != 0u)
    {
        // 颜色图缩小过。透明度单独按原图尺寸保存，每个像素只占一个字节。
        uint header = image.w - 1u;
        image.xyz = uvec3(header + 2u, texels[header], texels[header + 1u]);
        bytesPerPixel = 1u;
        channel = 0u;
    }
    // UV 指向相邻像素之间时，按距离混合四个值。图像边缘的下一个像素取自另一侧。
    vec2 p = fract(uv) * vec2(image.yz) - .5;
    ivec2 q = ivec2(floor(p));
    vec2 w = fract(p);
    float top = mix(maskTexel(image.xyz, q, bytesPerPixel, channel),
                    maskTexel(image.xyz, q + ivec2(1, 0), bytesPerPixel, channel), w.x);
    float bottom = mix(maskTexel(image.xyz, q + ivec2(0, 1), bytesPerPixel, channel),
                       maskTexel(image.xyz, q + ivec2(1, 1), bytesPerPixel, channel), w.x);
    return mix(top, bottom, w.y);
}

bool passesAlphaMask(float signedFace, vec2 weights)
{
    TextureFace face = faces[uint(-signedFace) - 1u];
    vec2 uv = face.uv01.xy * (1. - weights.x - weights.y) + face.uv01.zw * weights.x + face.uv2.xy * weights.y;
    Mask mask = alphaMasks[uint(face.uv2.w)];
    // RGBA 从 0 开始编号。底色贴图用 A，独立透明贴图用 R，二者在同一处 UV 取值。
    float alpha = sampleMask(face.image, uv, 3u) * mask.parameters.x * sampleMask(mask.opacity, uv, 0u);
    return alpha >= mask.parameters.y;
}
