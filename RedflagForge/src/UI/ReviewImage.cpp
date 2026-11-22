#include "rfpch.h"
#include "ReviewImage.h"
#include "Platform/Vulkan/VulkanDraw.h"
#include "../../../Redflag/vendor/stb_image/stb_image.h"

namespace Redflag
{
    void ReviewImage::Load(VulkanDraw& device, const std::vector<uint8_t>& bytes)
    {
        if (bytes.size() > INT_MAX) throw std::runtime_error("图片超过解码器支持的大小");
        int channels;
        stbi_set_flip_vertically_on_load(1);
        std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
            stbi_load_from_memory(bytes.data(), (int)bytes.size(), &Width, &Height, &channels, 4), stbi_image_free);
        if (!pixels) throw std::runtime_error("图片解码失败");
        Texture = device.CreateTexture(Width, Height, pixels.get());
        ID = (ImTextureID)device.GetPreviewTexture(Texture);
    }

    void ReviewImage::Draw(float width) const
    {
        width = std::min(width, float(Width));
        ImGui::Image(ID, {width, width * Height / Width}, {0, 1}, {1, 0});
    }
} // namespace Redflag
