#include "rfpch.h"
#include "EnvironmentLighting.h"
#include "IBLArchive.h"
#include "IBLPrecompute.h"
#include <stb_image.h>
#include <cctype>
#include <climits>
#include <fstream>

namespace Redflag::EnvironmentLighting
{
    namespace
    {
        // 每层紧接在上一层后。着色器用同样的顺序在经纬图中查找亮度。
        std::vector<glm::vec4> Panorama(const std::filesystem::path& path, glm::ivec2& size)
        {
            int width, height, channels;
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file || file.tellg() <= 0 || file.tellg() > INT_MAX) return {};
            std::vector<stbi_uc> encoded(static_cast<size_t>(file.tellg()));
            file.seekg(0);
            if (!file.read(reinterpret_cast<char*>(encoded.data()), encoded.size())) return {};
            if (!stbi_is_hdr_from_memory(encoded.data(), static_cast<int>(encoded.size()))) return {};
            stbi_set_flip_vertically_on_load(1);
            std::unique_ptr<float, decltype(&stbi_image_free)> source(
                stbi_loadf_from_memory(encoded.data(), static_cast<int>(encoded.size()), &width, &height, &channels, STBI_rgb_alpha),
                stbi_image_free);
            if (!source) return {};

            // 镜面最清晰的一面只有 128 像素。先把 4K 以上的天光平均到小图，保留小片高亮的能量。
            int stride = std::max({1, (width + 1023) / 1024, (height + 511) / 512});
            size = {(width + stride - 1) / stride, (height + stride - 1) / stride};
            std::vector<glm::vec4> pixels(size_t(size.x) * size.y, glm::vec4(0));
            const float* rgb = source.get();
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x, rgb += 4)
                    pixels[(y / stride) * size.x + x / stride] += glm::vec4(rgb[0], rgb[1], rgb[2], 1);
            for (auto& pixel : pixels) pixel /= pixel.w;

            size_t offset = 0;
            for (glm::ivec2 level = size; level.x > 1 || level.y > 1; )
            {
                glm::ivec2 next = glm::max(level / 2, glm::ivec2(1));
                size_t nextOffset = pixels.size();
                pixels.resize(nextOffset + size_t(next.x) * next.y);
                for (int y = 0; y < next.y; ++y)
                    for (int x = 0; x < next.x; ++x)
                    {
                        glm::ivec2 first = glm::ivec2(x,y) * level / next;
                        glm::ivec2 end = glm::ivec2(x+1,y+1) * level / next;
                        glm::vec4 sum(0);
                        for (int sy = first.y; sy < end.y; ++sy)
                            for (int sx = first.x; sx < end.x; ++sx)
                                sum += pixels[offset + sy * level.x + sx];
                        pixels[nextOffset + y * next.x + x] = sum / float((end.x-first.x) * (end.y-first.y));
                    }
                offset = nextOffset;
                level = next;
            }
            return pixels;
        }
    }

    Ref<ComputeResource> Load(ComputeDevice& device, const std::filesystem::path& path)
    {
        std::string extension = path.extension().u8string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) { return std::tolower(ch); });
        if (extension == ".rfibl")
        {
            std::vector<glm::vec4> pixels;
            if (!IBLArchive::ReadLighting(path, pixels)) return {};
            return device.CreateBuffer(pixels.size() * sizeof(glm::vec4), pixels.data());
        }

        glm::ivec2 size;
        if (extension != ".hdr") return {};
        auto pixels = Panorama(path, size);
        if (pixels.empty()) return {};

        static const uint32_t code[] =
#include "ScreenSpace/Shaders/BakeEnvironment.comp.inc"
        ;
        auto bake = device.CreatePipeline(code, sizeof(code), {ComputeBinding::Buffer, ComputeBinding::Buffer}, sizeof(size));
        auto panorama = device.CreateBuffer(pixels.size() * sizeof(glm::vec4), pixels.data());
        uint32_t count = 6 * IBLTextures::DiffuseSize * IBLTextures::DiffuseSize + IBLTextures::LutSize * IBLTextures::LutSize;
        for (int level = 0; level < IBLTextures::ReflectionLevels; ++level)
        {
            uint32_t width = IBLTextures::ReflectionSize >> level;
            count += 6 * width * width;
        }
        auto result = device.CreateBuffer(size_t(count) * sizeof(glm::vec4));
        device.Dispatch(bake, {panorama, result}, &size, (count + 63) / 64);
        return result;
    }
}
