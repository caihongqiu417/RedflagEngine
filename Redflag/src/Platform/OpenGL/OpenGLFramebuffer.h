#pragma once

#include"Redflag/Renderer/Framebuffer.h"

namespace Redflag {

	/* 保存这次绘制用的颜色图和深度图。视口改变大小时，按新尺寸重建这些图。 */
	class OpenGLFramebuffer :public Framebuffer
	{
	public:
		// ---- 初始化与销毁 ----
		OpenGLFramebuffer(const FramebufferSpecification& spec);
		virtual ~OpenGLFramebuffer();

		void Invalidate();

		// ---- 目标操作 ----
		virtual void Bind() override;
		virtual void Unbind() override;

		virtual void Resize(uint32_t width, uint32_t height)override;

		// ---- 附件读写 ----
		virtual int ReadPixel(uint32_t attachmentIndex, int x, int y) override;

		virtual void ClearAttachment(uint32_t attachmentIndex, int value)override;

		// ---- OpenGL 对象查询 ----
		virtual uint32_t GetColorAttachmentRendererID(uint32_t index = 0) const override;

		// 阴影计算使用这张深度图，比较物体和灯光之间有没有遮挡。
		virtual uint32_t GetDepthAttachmentRendererID() const override;

		// 在两个绘制目标之间复制图像时，OpenGL 需要这个帧缓冲对象的编号。
		virtual uint32_t GetFramebufferID() const override { return m_RendererID; }

		const FramebufferSpecification& OpenGLFramebuffer::GetSpecifictaion() const
		{
			return m_Specification;
		}
	private:
		// ---- 目标规格与附件 ----
		uint32_t m_RendererID=0;
		FramebufferSpecification m_Specification;

		std::vector<FramebufferTextureSpecification> m_ColorAttachmentSpecifications;
		FramebufferTextureSpecification m_DepthAttachmentSpecifications = FramebufferTextureFormat::None;

		std::vector<uint32_t> m_ColorAttachments;
		uint32_t m_DepthAttachment = 0;
	};
}