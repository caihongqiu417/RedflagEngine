#pragma once
#include "Redflag/Renderer/Texture.h"

namespace Redflag
{
	/* 材质保存 Texture2D。绘制网格时，GetSampledTexture 把这张贴图交给 Vulkan 的绘制命令。 */
	class VulkanTexture2D final : public Texture2D
	{
	public:
		VulkanTexture2D(uint32_t width, uint32_t height) : m_Width(width), m_Height(height) {}

		explicit VulkanTexture2D(const std::string& path);

		void SetData(void* data, uint32_t size) override;

		Ref<SampledTexture> GetSampledTexture() override { return m_Image; }

		bool IsLoaded() const override { return static_cast<bool>(m_Image); }

		uint32_t GetWidth() const override { return m_Width; }

		uint32_t GetHeight() const override { return m_Height; }

		const std::string& GetPath() const override { return m_Path; }

		bool operator==(const Texture& other) const override { return this == &other; }

		// 旧缩略图接口需要 OpenGL 纹理 ID。这里返回 0，让它跳过这张 Vulkan 贴图。
		uint32_t GetRendererID() const override { return 0; }

		void Bind(uint32_t slot) const override;

	private:
		Ref<SampledTexture> m_Image;
		uint32_t m_Width = 0, m_Height = 0;
		std::string m_Path;
	};
}
