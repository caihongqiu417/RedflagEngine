#include "MaskCoverage.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace Redflag
{
    namespace
    {
        struct AxisSample
        {
            unsigned First, Second;
            float Weight;
        };

        using std::size_t;

        std::vector<AxisSample> PrepareAxis(size_t samples, unsigned extent)
        {
            std::vector<AxisSample> axis(samples);
            for (size_t i = 0; i < samples; ++i)
            {
                // UV=0 位于首尾两个纹素之间。减去半格，让整数坐标落在纹素中心。
                const double source = (i + .5) * extent / samples - .5;
                const int first = static_cast<int>(std::floor(source));
                const unsigned left = first < 0 ? extent - 1 : static_cast<unsigned>(first);
                axis[i] = {left, (left + 1) % extent, static_cast<float>(source - first)};
            }
            return axis;
        }

        struct ChannelSampler
        {
            MaskChannel Channel;
            std::vector<AxisSample> Columns, Rows;

            float Sample(size_t x, size_t y) const
            {
                if (Channel.Width == 1 && Channel.Height == 1) return Channel.Pixels[0] / 255.f;
                const AxisSample& col = Columns[x];
                const AxisSample& row = Rows[y];
                auto value = [&](unsigned sx, unsigned sy) {
                    return float(Channel.Pixels[(size_t(sy) * Channel.Width + sx) * Channel.Stride]) / 255.f;
                };
                float a = value(col.First, row.First), b = value(col.First, row.Second);
                a += col.Weight * (value(col.Second, row.First) - a);
                b += col.Weight * (value(col.Second, row.Second) - b);
                return a + row.Weight * (b - a);
            }
        };
    }

    std::vector<CoverageMip> BuildMaskCoverage(MaskChannel baseAlpha, MaskChannel opacity, float materialAlpha,
                                              float cutoff, unsigned samplesPerAxis)
    {
        if (!samplesPerAxis) throw std::invalid_argument("Mask coverage requires at least one sample per axis");
        const std::uint8_t white = 255;
        if (!baseAlpha.Pixels) baseAlpha = {&white};
        if (!opacity.Pixels) opacity = {&white};
        const unsigned width = std::max(baseAlpha.Width, opacity.Width), height = std::max(baseAlpha.Height, opacity.Height);
        const size_t samplesPerCell = size_t(samplesPerAxis) * samplesPerAxis;
        auto sampler = [&](MaskChannel channel) {
            if (!channel.Width || !channel.Height || !channel.Stride)
                throw std::invalid_argument("Mask channel width, height and stride must be positive");
            return ChannelSampler{channel, PrepareAxis(width * samplesPerCell, channel.Width),
                                  PrepareAxis(height * samplesPerCell, channel.Height)};
        };
        const ChannelSampler base = sampler(baseAlpha), red = sampler(opacity);

        // 一格分成 8×8 小格时，各行在小格内的横坐标略微错开。细竖线因此能落在 64 个不同的横坐标上。
        const size_t pitch = size_t(width) + 1;
        std::vector<std::uint64_t> sum(pitch * (size_t(height) + 1));
        for (unsigned y = 0; y < height; ++y)
        {
            std::uint64_t rowTotal = 0;
            for (unsigned x = 0; x < width; ++x)
            {
                for (unsigned j = 0; j < samplesPerAxis; ++j)
                    for (unsigned i = 0; i < samplesPerAxis; ++i)
                    {
                        const size_t sx = x * samplesPerCell + size_t(i) * samplesPerAxis + j;
                        const size_t sy = y * samplesPerCell + size_t(j) * samplesPerAxis + i;
                        if (base.Sample(sx, sy) * materialAlpha * red.Sample(sx, sy) >= cutoff) ++rowTotal;
                    }
                sum[(size_t(y) + 1) * pitch + x + 1] = sum[size_t(y) * pitch + x + 1] + rowTotal;
            }
        }

        auto integral = [&](double x, double y) {
            const unsigned ix = static_cast<unsigned>(x), iy = static_cast<unsigned>(y);
            const unsigned jx = std::min(ix + 1, width), jy = std::min(iy + 1, height);
            auto at = [&](unsigned a, unsigned b) { return static_cast<double>(sum[size_t(b) * pitch + a]); };
            const double u = x - ix, v = y - iy;
            const double lower = (1 - u) * at(ix, iy) + u * at(jx, iy);
            const double upper = (1 - u) * at(ix, jy) + u * at(jx, jy);
            return (1 - v) * lower + v * upper;
        };

        // 每一级都从同一张计数表取面积，最后才转为 R8。奇数宽度的图也会计算最右一列。
        std::vector<CoverageMip> levels;
        for (unsigned w = width, h = height; ; w = std::max(1u, w / 2), h = std::max(1u, h / 2))
        {
            CoverageMip level{w, h, std::vector<std::uint8_t>(size_t(w) * h)};
            const double samples = (double(width) / w) * (double(height) / h) * samplesPerAxis * samplesPerAxis;
            for (unsigned y = 0; y < h; ++y)
                for (unsigned x = 0; x < w; ++x)
                {
                    const double x0 = double(x) * width / w, x1 = double(x + 1) * width / w;
                    const double y0 = double(y) * height / h, y1 = double(y + 1) * height / h;
                    const double passed = integral(x1, y1) - integral(x0, y1) - integral(x1, y0) + integral(x0, y0);
                    level.Pixels[size_t(y) * w + x] = static_cast<std::uint8_t>(std::lround(255 * passed / samples));
                }
            levels.push_back(std::move(level));
            if (w == 1 && h == 1) return levels;
        }
    }
}
