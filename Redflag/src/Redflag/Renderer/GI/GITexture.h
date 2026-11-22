#pragma once

#include "GIScene.h"
#include <map>

struct aiScene;
struct aiString;
struct aiTexture;

namespace Redflag
{
    enum class GIMaskChannel { None, Alpha, Red };

    struct GIImageSource
    {
        std::filesystem::path Path;
        const aiTexture* Embedded = nullptr;
    };

    GIImageSource FindGIImage(const aiScene& model, const aiString& name, const std::filesystem::path& modelPath);

    /* 同一张图可以既作底色，又作透明贴图。先登记镂空材质要用的通道，读图时一起取出。 */
    class GITextureLibrary
    {
    public:
        void KeepMask(const GIImageSource& source, GIMaskChannel channel);
        glm::uvec4 Read(GIScene& scene, const GIImageSource& source,
                        GIMaskChannel channel = GIMaskChannel::None, bool shareAcrossParts = false);

    private:
        struct Entry
        {
            bool KeepAlpha = false, KeepRed = false, Loaded = false;
            glm::uvec4 Image{};
            uint32_t Alpha = 0, Red = 0;
        };

        std::map<std::filesystem::path, Entry> m_MaskImages, m_Overrides;
    };
}
