#pragma once

#include "Redflag/Core/Core.h"
#include "Redflag/Renderer/Framebuffer.h"
#include "Redflag/Renderer/Shader.h"
#include "Redflag/Renderer/UniformBuffer.h"

#include <array>

namespace Redflag
{

	// ---- 后处理参数 ----
	struct BloomSettings
	{
		bool Enabled = true;
		float Threshold = 0.8f;  // 按线性亮度挑出泛光的亮区，最低取 0.5
		float Intensity = 0.6f;  // 合成时加入多少泛光，范围为 0 到 1
		float Radius = 2.0f;  // 模糊半径，用正在读取的那层图的像素计
		int MipLevels = 6; // 使用 1~6 层，首层为输入宽高的一半
	};

	struct ToneMapSettings
	{
		float Exposure = 1.0f;  // 映射到显示范围之前先乘这个值，1 保留原亮度
	};

	/*
	 * 为一个视口绘制泛光，再将场景亮度压到显示范围。
	 * 场景颜色图由调用方提供，泛光层和最终输出由这里持有。
	 * 创建、使用和销毁时都需要有效的图形上下文。
	 * 绘制结束时目标仍是输出图，深度测试开启，混合关闭。继续画场景前要先切回场景目标。
	 */
	class PostProcess
	{
	public:
		// ---- 初始化与销毁 ----
		PostProcess();
		~PostProcess();
		PostProcess(const PostProcess&) = delete;
		PostProcess& operator=(const PostProcess&) = delete;

		// ---- 执行与输出 ----
		// 读取场景的 HDR 颜色，宽高按像素计。输入无效时，查询本帧输出会得到 0。
		void Render(uint32_t color /* 借用的纹理 ID */, int width, int height);
		// 输出纹理由后处理持有，视口尺寸变化后应重新取 ID。
		uint32_t GetColorTextureID() const;
		uint32_t GetBloomTextureID() const;

		// ---- 参数 ----
		BloomSettings& GetBloomSettings() { return m_BloomSettings; }
		ToneMapSettings& GetToneMapSettings() { return m_ToneMapSettings; }

	private:
		// ---- Pass ----
		void RenderBloom(uint32_t color, int width, int height);
		void Composite(uint32_t color, int width, int height);
		void DrawTriangle();

	private:
		// ---- 视口输出 ----
		static constexpr int MaxMips = 6;
		std::array<Ref<Framebuffer>, MaxMips> m_BloomMips;
		Ref<Framebuffer> m_Output;
		bool m_OutputValid = false;

		// ---- 绘制资源 ----
		Ref<Shader> m_BrightnessShader;
		Ref<Shader> m_UpsampleShader;
		Ref<Shader> m_CompositeShader;
		Ref<UniformBuffer> m_Params;
		uint32_t m_BlackTexture = 0;
		uint32_t m_VertexArray = 0;
		uint32_t m_VertexBuffer = 0;

		// ---- 状态 ----
		BloomSettings m_BloomSettings;
		ToneMapSettings m_ToneMapSettings;
	};

}
