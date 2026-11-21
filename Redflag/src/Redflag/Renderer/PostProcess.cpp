#include "rfpch.h"
#include "PostProcess.h"
#include "Redflag/Renderer/Renderer3D.h"

#include <glad/glad.h>
#include <algorithm>

namespace Redflag
{

	// std140，与三个后处理 Shader 的 binding 5 一致。
	struct BloomParams
	{
		glm::vec4 A; // Threshold, Intensity, Radius
		glm::vec4 B; // TexelSize.xy, Exposure
	};

	PostProcess::PostProcess()
	{
		FramebufferSpecification spec;
		spec.Width = spec.Height = 1;
		spec.Attachments = { FramebufferTextureFormat::RGBA16F };
		for (auto& mip : m_BloomMips)
			mip = Framebuffer::Create(spec);
		spec.Attachments = { FramebufferTextureFormat::RGBA8 };
		m_Output = Framebuffer::Create(spec);

		m_BrightnessShader = Shader::Create("assets/shaders/Bloom_Brightness.glsl");
		m_UpsampleShader = Shader::Create("assets/shaders/Bloom_Upsample.glsl");
		m_CompositeShader = Shader::Create("assets/shaders/PostProcess_Composite.glsl");
		m_Params = UniformBuffer::Create(sizeof(BloomParams), 5);

		// Bloom 关闭时仍绑定有效纹理，间接贡献为零。
		glCreateTextures(GL_TEXTURE_2D, 1, &m_BlackTexture);
		glTextureStorage2D(m_BlackTexture, 1, GL_RGBA8, 1, 1);
		uint32_t black = 0;
		glTextureSubImage2D(m_BlackTexture, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, &black);
		glTextureParameteri(m_BlackTexture, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTextureParameteri(m_BlackTexture, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

		const float vertices[] = { -1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f };
		glCreateVertexArrays(1, &m_VertexArray);
		glCreateBuffers(1, &m_VertexBuffer);
		glNamedBufferData(m_VertexBuffer, sizeof(vertices), vertices, GL_STATIC_DRAW);
		glVertexArrayVertexBuffer(m_VertexArray, 0, m_VertexBuffer, 0, sizeof(float) * 2);
		glEnableVertexArrayAttrib(m_VertexArray, 0);
		glVertexArrayAttribFormat(m_VertexArray, 0, 2, GL_FLOAT, GL_FALSE, 0);
		glVertexArrayAttribBinding(m_VertexArray, 0, 0);
	}

	PostProcess::~PostProcess()
	{
		glDeleteTextures(1, &m_BlackTexture);
		glDeleteBuffers(1, &m_VertexBuffer);
		glDeleteVertexArrays(1, &m_VertexArray);
	}

	void PostProcess::Render(uint32_t color, int width, int height)
	{
		m_OutputValid = false;
		if (!color || width <= 0 || height <= 0)
			return;

		glDisable(GL_DEPTH_TEST);
		glDisable(GL_BLEND);
		if (m_BloomSettings.Enabled)
			RenderBloom(color, width, height);
		Composite(color, width, height);
		glEnable(GL_DEPTH_TEST);
		m_OutputValid = true;
	}

	void PostProcess::RenderBloom(uint32_t color, int width, int height)
	{
		const int levels = std::clamp(m_BloomSettings.MipLevels, 1, MaxMips);
		for (int i = 0; i < levels; ++i)
		{
			uint32_t w = std::max(width >> (i + 1), 1);
			uint32_t h = std::max(height >> (i + 1), 1);
			const auto& spec = m_BloomMips[i]->GetSpecifictaion();
			if (spec.Width != w || spec.Height != h)
				m_BloomMips[i]->Resize(w, h);
		}

		// 高光提取：完整覆盖 mip 0，不需要预清空所有层。
		m_BloomMips[0]->Bind();
		glDrawBuffer(GL_COLOR_ATTACHMENT0);
		m_BrightnessShader->Bind();
		BloomParams params{};
		params.A.x = std::max(m_BloomSettings.Threshold, 0.5f);
		m_Params->SetData(&params, sizeof(params));
		glBindTextureUnit(0, color);
		DrawTriangle();
		Renderer3D::LogExternalDrawCall(Renderer3D::DrawCallInfo::PassType::BloomBright, 3);

		// 下采样：每层写入独立目标，避免同一纹理读写。
		for (int i = 0; i + 1 < levels; ++i)
		{
			const auto& source = m_BloomMips[i]->GetSpecifictaion();
			const auto& target = m_BloomMips[i + 1]->GetSpecifictaion();
			glBindFramebuffer(GL_READ_FRAMEBUFFER, m_BloomMips[i]->GetFramebufferID());
			glReadBuffer(GL_COLOR_ATTACHMENT0);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_BloomMips[i + 1]->GetFramebufferID());
			glDrawBuffer(GL_COLOR_ATTACHMENT0);
			glBlitFramebuffer(0, 0, source.Width, source.Height, 0, 0, target.Width, target.Height,
				GL_COLOR_BUFFER_BIT, GL_LINEAR);
		}

		// 上采样：将较小层加到上一层。
		m_UpsampleShader->Bind();
		glEnable(GL_BLEND);
		glBlendEquation(GL_FUNC_ADD);
		glBlendFunc(GL_ONE, GL_ONE);
		for (int i = levels - 1; i >= 1; --i)
		{
			const auto& source = m_BloomMips[i]->GetSpecifictaion();
			params.A = glm::vec4(m_BloomSettings.Threshold, m_BloomSettings.Intensity,
				std::max(m_BloomSettings.Radius, 0.1f), 0.0f);
			params.B = glm::vec4(1.0f / source.Width, 1.0f / source.Height, 0.0f, 0.0f);
			m_Params->SetData(&params, sizeof(params));
			glBindTextureUnit(0, m_BloomMips[i]->GetColorAttachmentRendererID());
			m_BloomMips[i - 1]->Bind();
			glDrawBuffer(GL_COLOR_ATTACHMENT0);
			DrawTriangle();
			Renderer3D::LogExternalDrawCall(Renderer3D::DrawCallInfo::PassType::BloomUp, 3);
		}
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glDisable(GL_BLEND);
	}

	void PostProcess::Composite(uint32_t color, int width, int height)
	{
		const auto& spec = m_Output->GetSpecifictaion();
		if (spec.Width != width || spec.Height != height)
			m_Output->Resize(width, height);
		m_Output->Bind();
		glDrawBuffer(GL_COLOR_ATTACHMENT0);
		m_CompositeShader->Bind();
		glBindTextureUnit(0, color);
		glBindTextureUnit(1, m_BloomSettings.Enabled
			? m_BloomMips[0]->GetColorAttachmentRendererID() : m_BlackTexture);

		const auto& bloom = m_BloomMips[0]->GetSpecifictaion();
		BloomParams params;
		params.A = glm::vec4(m_BloomSettings.Threshold,
			glm::clamp(m_BloomSettings.Intensity, 0.0f, 1.0f),
			glm::clamp(m_BloomSettings.Radius, 0.1f, 4.0f), 0.0f);
		params.B = glm::vec4(1.0f / bloom.Width, 1.0f / bloom.Height,
			glm::max(m_ToneMapSettings.Exposure, 0.001f), 0.0f);
		m_Params->SetData(&params, sizeof(params));
		DrawTriangle();
		Renderer3D::LogExternalDrawCall(Renderer3D::DrawCallInfo::PassType::BloomTonemap, 3);
	}

	void PostProcess::DrawTriangle()
	{
		glBindVertexArray(m_VertexArray);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glBindVertexArray(0);
	}

	uint32_t PostProcess::GetColorTextureID() const
	{
		return m_OutputValid ? m_Output->GetColorAttachmentRendererID() : 0;
	}

	uint32_t PostProcess::GetBloomTextureID() const
	{
		return m_OutputValid && m_BloomSettings.Enabled
			? m_BloomMips[0]->GetColorAttachmentRendererID() : 0;
	}

}
