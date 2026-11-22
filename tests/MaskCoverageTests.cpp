#include "coverage/MaskCoverage.h"
#define STBI_ONLY_PNG
#define STB_IMAGE_IMPLEMENTATION
#include "../Redflag/vendor/stb_image/stb_image.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>

using Redflag::BuildMaskCoverage;
using Redflag::CoverageMip;
using Redflag::MaskChannel;

static void Expect(bool valid, const char* message)
{
    if (!valid) throw std::runtime_error(message);
}

static int Coarsest(MaskChannel base, MaskChannel red, float alpha, float cutoff, unsigned samples = 8)
{
    return BuildMaskCoverage(base, red, alpha, cutoff, samples).back().Pixels[0];
}

static void CheckMaterials()
{
    const std::array<std::uint8_t, 4> wire{255, 0, 0, 0}, shifted{0, 0, 255, 0};
    const MaskChannel horizontal{wire.data(), 4, 1}, vertical{wire.data(), 1, 4};
    const std::uint8_t uniform = 64, alpha = 204;

    Expect(Coarsest(horizontal, {}, 1.f, .5f) == 64, "Every fourth texel should cover one quarter");
    Expect(Coarsest({&uniform}, {}, 1.f, .5f) == 0, "A spatially constant 64/255 must be rejected");
    Expect(Coarsest({}, horizontal, 1.f, .75f) == 32, "A stricter cutoff halves the wire width");
    Expect(Coarsest(horizontal, {}, .5f, .375f) == 32, "Material tint must affect the sampled cutoff");
    Expect(Coarsest({&alpha}, {}, .625f, .5f) == 255, "Equal to the material cutoff must pass");
    Expect(Coarsest({&alpha}, {}, .625f, .501f) == 0, "A slightly stricter cutoff must reject");
    Expect(Coarsest({}, {}, 0.f, 0.f) == 255 && Coarsest({}, {}, 0.f, .001f) == 0, "Zero-alpha endpoints");

    // 同一条渐变分别存到 A 与 R 时，A*R >= 0.25 与 A >= 0.5 选中的面积相同。
    Expect(Coarsest(horizontal, horizontal, 1.f, .25f) == 64, "Correlated A and R");
    Expect(Coarsest(horizontal, {shifted.data(), 4, 1}, 1.f, .25f) == 0, "Disjoint A and R");

    // A 沿 x 线性升降，R 沿 y 线性升降。四个对称的小方块里分别积分 1 - cutoff/a。
    const double cutoff = .5;
    const double exact = (1 - cutoff + cutoff * std::log(cutoff)) / 4;
    const int crossed = Coarsest(horizontal, vertical, 1.f, float(cutoff));
    Expect(std::abs(crossed - 255 * exact) <= 2, "Crossed gradients must be filtered as a product at one UV");
    Expect(std::abs(Coarsest(horizontal, vertical, 1.f, float(cutoff), 32) - 255 * exact) <= 1, "Refined crossed gradients");

    const double thin = 2 * (1 - .98) / 4;
    Expect(std::abs(Coarsest(horizontal, {}, 1.f, .98f) - 255 * thin) <= 1, "Sub-texel wire near cutoff one");
    std::cout << "Crossed A and R: " << crossed << "/255, exact " << exact
              << "; thin wire at 0.98: " << Coarsest(horizontal, {}, 1.f, .98f) << "/255, exact " << thin << '\n';
}

static void CheckOddSizes()
{
    const std::array<std::uint8_t, 15> edge{0,0,0,0,255, 0,0,0,0,255, 0,0,0,0,255};
    const auto mips = BuildMaskCoverage({edge.data(), 5, 3}, {}, 1.f, .5f);
    Expect(mips.size() == 3 && mips[1].Width == 2 && mips[1].Height == 1, "5x3 -> 2x1 -> 1x1");
    Expect(mips[0].Pixels == std::vector<std::uint8_t>(edge.begin(), edge.end()), "Reference at original size");
    Expect(mips[1].Pixels[0] == 0 && mips[1].Pixels[1] == 102, "Last column covers 1 / 2.5 of the second cell");
    Expect(mips[2].Pixels[0] == 51, "Whole image includes its last column");
}

static void CheckWireImages(const char* baseFile, const char* opacityFile)
{
    std::vector<CoverageMip> previous;
    for (int image = 0; image < 2; ++image)
    {
        int width, height, channels;
        std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> data(
            stbi_load(image == 0 ? baseFile : opacityFile, &width, &height, &channels, 4), stbi_image_free);
        Expect(bool(data) && width == 1024 && height == 32, "Run from repo root with the fine-mask fixture PNGs");
        MaskChannel channel{data.get() + (image == 0 ? 3 : 0), unsigned(width), unsigned(height), 4};
        const auto mips = BuildMaskCoverage(image == 0 ? channel : MaskChannel{}, image == 1 ? channel : MaskChannel{}, 1.f, .5f);
        Expect(mips.size() == 11 && mips.back().Pixels.front() == 64, "1024x32 -> 1x1 retains wire coverage");
        for (size_t level = 0; level < mips.size(); ++level)
        {
            if (level >= 2)
                Expect(std::all_of(mips[level].Pixels.begin(), mips[level].Pixels.end(),
                                   [](std::uint8_t c) { return c == 64; }), "Each 4-texel interval contains exactly one wire");
            if (image == 1) Expect(mips[level].Pixels == previous[level].Pixels, "A and red-map images must match at every level");
        }
        previous = mips;
    }
    std::cout << "1024x32 alpha and red PNGs match at all 11 levels; coverage 64/255 at 4 texels and beyond.\n";
}

static void CheckSponza(const char* file)
{
    int width, height, channels;
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> data(stbi_load(file, &width, &height, &channels, 4), stbi_image_free);
    Expect(data && width == 1024 && height == 1024, "Expected an original Sponza 1024x1024 cutout image");
    const auto levels = BuildMaskCoverage({data.get() + 3, unsigned(width), unsigned(height), 4}, {}, 1.f, .5f);
    const CoverageMip& level = levels[3]; // 这一层每格覆盖原图的 8×8 个纹素。
    auto alpha = [&](int x, int y) {
        return int(data.get()[4 * (((y + height) % height) * width + (x + width) % width) + 3]);
    };

    // 在有轮廓穿过的 32 格内，各用另一套 256×256 均匀采样作为参考。
    double sumError = 0, maxError = 0;
    int checked = 0;
    for (size_t i = 0; i < level.Pixels.size() && checked < 32; ++i)
    {
        const size_t cell = i * 7919 % level.Pixels.size();
        const int coverage = level.Pixels[cell];
        if (coverage <= 8 || coverage >= 247) continue;
        const int left = int(cell % level.Width) * 8, top = int(cell / level.Width) * 8;
        int hits = 0;
        for (int y = 0; y < 256; ++y)
            for (int x = 0; x < 256; ++x)
            {
                const double u = left + (x + .5) / 32 - .5, v = top + (y + .5) / 32 - .5;
                const int a = int(std::floor(u)), b = int(std::floor(v));
                const double fx = u - a, fy = v - b;
                const double c = (1 - fy) * ((1 - fx)*alpha(a,b) + fx*alpha(a+1,b))
                               + fy * ((1 - fx)*alpha(a,b+1) + fx*alpha(a+1,b+1));
                if (c >= 127.5) ++hits;
            }
        const double error = std::abs(double(hits) / (256 * 256) - double(coverage) / 255);
        maxError = std::max(maxError, error);
        sumError += error;
        ++checked;
    }
    Expect(checked == 32 && maxError < .02, "Each selected 8x8 patch must be within 2 percentage points of the reference");
    std::cout << "Sponza: " << checked << " edge patches, mean abs error " << sumError / checked << ", max " << maxError << '\n';
}

int main(int argc, char** argv)
{
    try
    {
        CheckMaterials();
        CheckOddSizes();
        CheckWireImages(argc > 1 ? argv[1] : "output/validation/fine-mask/mask.png",
                        argc > 2 ? argv[2] : "output/validation/fine-mask/opacity.png");
        if (argc > 3) CheckSponza(argv[3]);
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
