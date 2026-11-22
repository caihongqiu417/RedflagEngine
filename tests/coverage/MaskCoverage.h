#pragma once

#include <cstdint>
#include <vector>

namespace Redflag
{
    struct MaskChannel
    {
        // 指向第一行的第一个通道值。RGBA 图取 A 时传 pixels + 3，Stride 为 4。两张图的行方向须一致。
        const std::uint8_t* Pixels = nullptr;
        unsigned Width = 1, Height = 1, Stride = 1;
    };

    struct CoverageMip
    {
        unsigned Width, Height;
        std::vector<std::uint8_t> Pixels; // 每个值是该格被材质遮住的面积比例，0 到 255 对应 0 到 100%。
    };

    /// 使用重复寻址和双线性采样。两张图取相同的 UV，缺图的通道按 1 计算。
    /// 更改图像、材质透明度或裁剪阈值后需要重建。返回的各级图按 R8 UNORM 读取。
    std::vector<CoverageMip> BuildMaskCoverage(MaskChannel baseAlpha, MaskChannel opacity,
                                              float materialAlpha, float cutoff, unsigned samplesPerAxis = 8);
}
