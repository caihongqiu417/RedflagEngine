#pragma once

#include "Redflag/Renderer/Shader.h"
#include <array>
#include <vector>

namespace Redflag
{
	/* 先保留玻璃后面的颜色和深度，再合成所有玻璃。编辑器的实体编号与法线留在主画面。 */
	class OpenGLTransparencyPass
	{
	public:
		OpenGLTransparencyPass();
		~OpenGLTransparencyPass();
		OpenGLTransparencyPass(const OpenGLTransparencyPass&) = delete;
		OpenGLTransparencyPass& operator=(const OpenGLTransparencyPass&) = delete;

		void Begin();
		void Composite();

	private:
		void Resize(int width, int height, int depthFormat);

		int m_Width = 0, m_Height = 0, m_DepthFormat = 0;
		uint32_t m_Target = 0, m_Snapshot = 0, m_VertexArray = 0;
		uint32_t m_Background = 0, m_Depth = 0;
		std::array<uint32_t, 3> m_Layers{};
		Ref<Shader> m_Composite;

		int m_SceneTarget = 0, m_Viewport[4]{};
		int m_DepthFunc = 0, m_CullMode = 0;
		unsigned char m_DepthWrite = 0, m_DepthTest = 0, m_CullFace = 0, m_Scissor = 0;
		struct ColorState
		{
			int SourceRGB, DestRGB, SourceAlpha, DestAlpha, EquationRGB, EquationAlpha;
			unsigned char Blend, Write[4];
		};
		std::array<ColorState, 3> m_ColorState;
		std::vector<uint32_t> m_DrawBuffers;
	};
}
