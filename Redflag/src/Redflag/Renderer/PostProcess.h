#pragma once

#include "Redflag/Core/Core.h"
#include "Redflag/Renderer/Framebuffer.h"
#include "Redflag/Renderer/Shader.h"
#include "Redflag/Renderer/UniformBuffer.h"

#include <array>

namespace Redflag
{

	// 后处理参数
	struct BloomSettings
	{
		bool Enabled = true;
		float Threshold = 0.8f;
		float Intensity = 0.6f;
		float Radius = 2.0f;
		int MipLevels = 6;
	};

	struct ToneMapSettings
	{
		float Exposure = 1.0f;
	};

	class PostProcess
	{
	public:
		PostProcess();
		~PostProcess();
		PostProcess(const PostProcess&) = delete;
		PostProcess& operator=(const PostProcess&) = delete;

		// 输入 HDR 颜色，输出独立的 LDR 目标；零尺寸表示视口不可绘制。
		void Render(uint32_t color, int width, int height);
		uint32_t GetColorTextureID() const;
		uint32_t GetBloomTextureID() const;

		BloomSettings& GetBloomSettings() { return m_BloomSettings; }
		ToneMapSettings& GetToneMapSettings() { return m_ToneMapSettings; }

	private:
		void RenderBloom(uint32_t color, int width, int height);
		void Composite(uint32_t color, int width, int height);
		void DrawTriangle();

	private:
		// 当前视口的输出资源
		static constexpr int MaxMips = 6;
		std::array<Ref<Framebuffer>, MaxMips> m_BloomMips;
		Ref<Framebuffer> m_Output;
		bool m_OutputValid = false;

		// 绘制资源
		Ref<Shader> m_BrightnessShader;
		Ref<Shader> m_UpsampleShader;
		Ref<Shader> m_CompositeShader;
		Ref<UniformBuffer> m_Params;
		uint32_t m_BlackTexture = 0;
		uint32_t m_VertexArray = 0;
		uint32_t m_VertexBuffer = 0;

		BloomSettings m_BloomSettings;
		ToneMapSettings m_ToneMapSettings;
	};

}
