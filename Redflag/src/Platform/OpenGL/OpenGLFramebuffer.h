#pragma once

#include"Redflag/Renderer/Framebuffer.h"

namespace Redflag {

	class OpenGLFramebuffer :public Framebuffer
	{
	public:
		OpenGLFramebuffer(const FramebufferSpecification& spec);
		virtual ~OpenGLFramebuffer();

		void Invalidate();

		virtual void Bind() override;
		virtual void Unbind() override;

		virtual void Resize(uint32_t width, uint32_t height)override;

		virtual int ReadPixel(uint32_t attachmentIndex, int x, int y) override;

		virtual void ClearAttachment(uint32_t attachmentIndex, int value)override;

		virtual uint32_t GetColorAttachmentRendererID(uint32_t index = 0) const override;

		// 返回深度附件的 GL 纹理 ID，供 shader 采样（如 Shadow Map）
		virtual uint32_t GetDepthAttachmentRendererID() const override;

		const FramebufferSpecification& OpenGLFramebuffer::GetSpecifictaion() const
		{
			return m_Specification;
		}
	private:
		uint32_t m_RendererID=0;
		FramebufferSpecification m_Specification;

		std::vector<FramebufferTextureSpecification> m_ColorAttachmentSpecifications;
		FramebufferTextureSpecification m_DepthAttachmentSpecifications = FramebufferTextureFormat::None;

		std::vector<uint32_t> m_ColorAttachments;
		uint32_t m_DepthAttachment = 0;
	};
}