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
		float Threshold = 0.8f; // 线性 HDR 亮度阈值，执行时下限为 0.5
		float Intensity = 0.6f; // Bloom 合成权重 [0,1]
		float Radius = 2.0f; // 上采样滤波半径，以当前源层纹素为单位
		int MipLevels = 6; // 使用 1~6 层，首层为输入宽高的一半
	};

	struct ToneMapSettings
	{
		float Exposure = 1.0f; // 色调映射前的线性曝光倍率，1 表示不缩放
	};

	/*
	 * 对已画好的场景图添加泛光，再把亮度压到显示范围；每个视口可持有一个实例。
	 * 输入图由调用方持有，中间的泛光层和最终输出由本对象持有。
	 * 构造、Render 和析构均在有效图形上下文中执行，多个实例的输出彼此独立。
	 * 有效输入下 Render 会切到输出帧缓冲，结束时深度测试开启、混合关闭；后续绘制须重新绑定目标。
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
		// 输入为 OpenGL 2D HDR 纹理，尺寸以像素计；空纹理或零尺寸使本帧输出失效。
		void Render(uint32_t color /* 借用的纹理 ID */, int width, int height);
		// 返回借用的 OpenGL 纹理 ID；未产生有效输出时为 0，Resize 后须重新查询。
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
