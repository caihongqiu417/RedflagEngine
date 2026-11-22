#include "rfpch.h"
#include "GITexture.h"
#include <assimp/scene.h>
#include <stb_image.h>
#include "Redflag/Renderer/ModelImages.h"

namespace Redflag
{
    GIImageSource FindGIImage(const aiScene& model, const aiString& name, const std::filesystem::path& modelPath)
    {
        const aiTexture* embedded = FindModelImage(model, name);
        if (!embedded) return {(modelPath.parent_path() / name.C_Str()).lexically_normal()};

        // 一张内嵌图可能同时有文件名和 *0 这样的编号，用模型名和图序号识别同一份像素。
        const auto index = std::find(model.mTextures, model.mTextures + model.mNumTextures, embedded) - model.mTextures;
        return {modelPath / ("*" + std::to_string(index)), embedded};
    }

    void GITextureLibrary::KeepMask(const GIImageSource& source, GIMaskChannel channel)
    {
        if (source.Path.empty()) return;
        Entry& entry = m_MaskImages[std::filesystem::absolute(source.Path).lexically_normal()];
        if (channel == GIMaskChannel::Alpha) entry.KeepAlpha = true;
        if (channel == GIMaskChannel::Red) entry.KeepRed = true;
    }

    glm::uvec4 GITextureLibrary::Read(GIScene& scene, const GIImageSource& source, GIMaskChannel channel, bool shareAcrossParts)
    {
        if (source.Path.empty()) return {};
        const std::filesystem::path key = std::filesystem::absolute(source.Path).lexically_normal();
        const auto found = m_MaskImages.find(key);
        Entry ordinary;
        // 老场景按替换图的原路径存放颜色。只有镂空贴图合并相对路径与绝对路径，保留旧烘焙的像素顺序。
        Entry& entry = found != m_MaskImages.end() ? found->second :
                       (shareAcrossParts ? m_Overrides[source.Path] : ordinary);
        auto descriptor = [&]() {
            glm::uvec4 image = entry.Image;
            if (channel == GIMaskChannel::Alpha) image.w = entry.Alpha;
            if (channel == GIMaskChannel::Red) image.w = entry.Red;
            return image;
        };
        if (entry.Loaded) return descriptor();
        entry.Loaded = true;

        // 把图片底行放在像素数组开头，让光照计算按模型的 UV 读图时，上下方向与绘制时一致。
        stbi_set_flip_vertically_on_load(1);
        int w = 0, h = 0, channels = 0;
        const aiTexture* embedded = source.Embedded;
        std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> bytes(nullptr, stbi_image_free);
        if (!embedded)
            bytes.reset(stbi_load(source.Path.string().c_str(), &w, &h, &channels, 4));
        else if (embedded->mHeight == 0)
            bytes.reset(stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(embedded->pcData), embedded->mWidth, &w,
                                              &h, &channels, 4));
        else
        {
            w = embedded->mWidth;
            h = embedded->mHeight;
        }
        if (!bytes && !(embedded && embedded->mHeight))
        {
            // 编辑器仍可显示材质颜色；烘焙时用这个文件名提示用户补回贴图。
            if (scene.UnreadableTexture.empty()) scene.UnreadableTexture = source.Path;
            return {};
        }

        auto rgba = [&](uint32_t x, uint32_t y) {
            if (bytes)
            {
                uint32_t pixel;
                std::memcpy(&pixel, bytes.get() + (size_t(y) * w + x) * 4, 4);
                return pixel;
            }
            const aiTexel& c = embedded->pcData[size_t(h - 1 - y) * w + x];
            return uint32_t(c.r) | uint32_t(c.g) << 8 | uint32_t(c.b) << 16 | uint32_t(c.a) << 24;
        };

        // 间接光只保留最长边不超过 512 像素的颜色图。调大这个尺寸能留下更多颜色细节，也会多用显存。
        const uint32_t offset = static_cast<uint32_t>(scene.Pixels.size());
        float scale = std::min(1.f, 512.f / std::max(w, h));
        uint32_t width = std::max(1l, std::lround(w * scale));
        uint32_t height = std::max(1l, std::lround(h * scale));
        scene.Pixels.resize(offset + size_t(width) * height);

        for (uint32_t y = 0; y < height; ++y)
            for (uint32_t x = 0; x < width; ++x)
            {
                unsigned sx = static_cast<unsigned>((x + .5f) * w / width);
                unsigned sy = static_cast<unsigned>((y + .5f) * h / height);
                scene.Pixels[offset + y * width + x] = rgba(sx, sy);
            }
        entry.Image = {offset, width, height, 0};

        // 缩小底色图会漏掉仅一像素宽的孔。透明度有变化时，另存它在原图上的每个值。
        auto keepChannel = [&](int channel) -> uint32_t {
            auto row = [&](int y) -> const uint8_t* {
                if (bytes) return bytes.get() + size_t(y) * w * 4 + channel;
                const uint8_t* bgra = reinterpret_cast<const uint8_t*>(embedded->pcData);
                return bgra + size_t(h - 1 - y) * w * sizeof(aiTexel) +
                       (channel == 3 ? offsetof(aiTexel, a) : offsetof(aiTexel, r));
            };
            const uint8_t first = *row(0);
            bool constant = true;
            for (int y = 0; y < h && constant; ++y)
            {
                const uint8_t* line = row(y);
                for (int x = 0; x < w; ++x)
                    if (line[4 * x] != first) { constant = false; break; }
            }
            if (constant) return 0;

            const size_t start = scene.Pixels.size();
            scene.Pixels.resize(start + 2 + (size_t(w) * h + 3) / 4);
            scene.Pixels[start] = w;
            scene.Pixels[start + 1] = h;
            for (int y = 0; y < h; ++y)
            {
                const uint8_t* line = row(y);
                for (int x = 0; x < w; ++x)
                {
                    const size_t i = size_t(y) * w + x;
                    scene.Pixels[start + 2 + i / 4] |= uint32_t(line[4 * x]) << (8 * (i % 4));
                }
            }
            return static_cast<uint32_t>(start + 1);
        };
        if (width != w || height != h)
        {
            if (entry.KeepAlpha && (!bytes || channels == 2 || channels == 4)) entry.Alpha = keepChannel(3);
            if (entry.KeepRed) entry.Red = keepChannel(0);
        }
        return descriptor();
    }
}
