#pragma once

#include "Redflag/Core/Core.h"
#include <filesystem>
#include <imgui.h>
#include <vector>

namespace Redflag
{
    class VulkanDraw;
    struct SampledTexture;

    /* 图片原有的宽高也保留下来，面板缩放时保持比例。 */
    struct ReviewImage
    {
        void Load(VulkanDraw& device, const std::vector<uint8_t>& encoded);
        void Draw(float width) const;

        Ref<SampledTexture> Texture;
        ImTextureID ID = nullptr;
        int Width = 0, Height = 0;
    };
} // namespace Redflag
