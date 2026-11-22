#include "rfpch.h"
#include "VulkanTexture2D.h"
#include "Redflag/Renderer/RHI/RenderDevice.h"
#include <stb_image.h>
#include <stdexcept>

namespace Redflag
{
	VulkanTexture2D::VulkanTexture2D(const std::string& path) : m_Path(path)
	{
		int width, height, channels;
		// 模型贴图沿用项目已有的 UV 朝向，第一行像素对应图的下边。
		stbi_set_flip_vertically_on_load(1);
		std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
			stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha), stbi_image_free);
		if (!pixels)
		{
			RF_CORE_WARN("Could not decode material texture {0}: {1}", path, stbi_failure_reason());
			return;
		}

		// 设备在返回前已取走像素，解码器可以释放原图。
		m_Width = width;
		m_Height = height;
		m_Image = RenderDevice::Get().CreateTexture(width, height, pixels.get());
	}

	void VulkanTexture2D::SetData(void* data, uint32_t size)
	{
		if (size != uint64_t(m_Width) * m_Height * 4)
			throw std::invalid_argument("Expected one RGBA8 pixel per texture coordinate");

		m_Image = RenderDevice::Get().CreateTexture(m_Width, m_Height, data);
	}

	void VulkanTexture2D::Bind(uint32_t) const
	{
		throw std::logic_error("Vulkan textures are selected when recording DrawIndexed");
	}
}
