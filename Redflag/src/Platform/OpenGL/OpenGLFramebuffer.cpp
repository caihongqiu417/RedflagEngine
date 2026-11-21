#include "rfpch.h"
#include "OpenGLFramebuffer.h"

#include<glad/glad.h>

namespace Redflag {

	namespace Utils {
		
		static GLenum TextureTarget(bool multisampled)
		{
			return multisampled ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
		}

		static void CreateTextures(bool multisampled, uint32_t* outID, uint32_t count)
		{
			glCreateTextures(TextureTarget(multisampled), count, outID);
		}

		static void BindTexture(bool multisampled, uint32_t id)
		{
			glBindTexture(TextureTarget(multisampled),id);
		}

		static void AttachColorTexture(uint32_t id, int samples, GLenum internalFormat, GLenum format, GLenum type, uint32_t width, uint32_t height, int index)
		{
			bool multisampled = samples > 1;
			if (multisampled)
			{
				glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples, internalFormat, width, height, GL_FALSE);
			}
			else
			{
				glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, type, nullptr);

				// 物体边缘两侧可能是不同的实体。采样编号纹理时只能取最近的像素，两个编号之间没有中间值。
				GLint filter = (type == GL_INT || type == GL_UNSIGNED_INT) ? GL_NEAREST : GL_LINEAR;
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			}
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + index, TextureTarget(multisampled), id, 0);
		}

		static void AttachDepthTexture(uint32_t id, int samples, GLenum format, GLenum attachmentType, uint32_t width, uint32_t height)
		{
			bool multisampled = samples > 1;
			if (multisampled)
			{
				glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples, format, width, height, GL_FALSE);
			}
			else
			{
				glTexStorage2D(GL_TEXTURE_2D, 1, format, width, height);

				// 物体边缘两侧的深度可能相差很大。软阴影要逐个像素比较深度，再把遮挡结果平均。
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			}
			glFramebufferTexture2D(GL_FRAMEBUFFER, attachmentType, TextureTarget(multisampled), id, 0);
		}

		static bool IsDepthFormat(FramebufferTextureFormat format)
		{
			switch (format)
			{
			case FramebufferTextureFormat::DEPTH24STENCIL8: return true;
			case FramebufferTextureFormat::DEPTH32F:        return true;
			}
			return false;
		}

		// 清除一张纹理时，告诉 OpenGL 每个像素要写几个分量。
		static GLenum RedflagFBTextureToGL(FramebufferTextureFormat format)
		{
			switch (format)
			{
			case FramebufferTextureFormat::RGBA8:        return GL_RGBA;
			case FramebufferTextureFormat::RGBA16F:      return GL_RGBA;
			case FramebufferTextureFormat::RG16F:        return GL_RG;
			case FramebufferTextureFormat::RED_INTEGER:  return GL_RED_INTEGER;
			}
			RF_CORE_ASSERT(false, "Unknown FramebufferTextureFormat in RedflagFBTextureToGL");
			return 0;
		}

		static GLenum GLDataType(FramebufferTextureFormat format)
		{
			switch (format)
			{
			case FramebufferTextureFormat::RGBA8:        return GL_UNSIGNED_BYTE;
			case FramebufferTextureFormat::RGBA16F:      return GL_FLOAT;
			case FramebufferTextureFormat::RG16F:        return GL_FLOAT;
			case FramebufferTextureFormat::RED_INTEGER:  return GL_INT;
			}
			RF_CORE_ASSERT(false, "Unknown FramebufferTextureFormat in GLDataType");
			return 0;
		}

	}
	
	OpenGLFramebuffer::OpenGLFramebuffer(const FramebufferSpecification& spec)
		:m_Specification(spec)
	{
		for (auto format : m_Specification.Attachments.Attachments)
		{
			if (!Utils::IsDepthFormat(format.TextureFormat))
				m_ColorAttachmentSpecifications.emplace_back(format);
			else
				m_DepthAttachmentSpecifications = format;
		}
		Invalidate();
	}

	OpenGLFramebuffer::~OpenGLFramebuffer()
	{
		glDeleteFramebuffers(1, &m_RendererID);
		glDeleteTextures(m_ColorAttachments.size(), m_ColorAttachments.data());
		glDeleteTextures(1, &m_DepthAttachment);
	}

	// ---- 附件重建 ----
	void OpenGLFramebuffer::Invalidate()
	{
		// 视口大小改变后会换一批纹理，编辑器预览要重新取纹理 ID。
		if (m_RendererID)
		{
			glDeleteFramebuffers(1, &m_RendererID);
			glDeleteTextures(m_ColorAttachments.size(),m_ColorAttachments.data());
			glDeleteTextures(1, &m_DepthAttachment);

			m_ColorAttachments.clear();
			m_DepthAttachment = 0;
		}

		glCreateFramebuffers(1, &m_RendererID);
		glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);

		bool multisample = m_Specification.Samples > 1;

		// ---- 颜色附件 ----
		if (m_ColorAttachmentSpecifications.size())
		{
			m_ColorAttachments.resize(m_ColorAttachmentSpecifications.size());
			Utils::CreateTextures(multisample, m_ColorAttachments.data(), m_ColorAttachments.size());

			for (size_t i = 0;i < m_ColorAttachments.size(); i++)
			{
				Utils::BindTexture(multisample, m_ColorAttachments[i]);
				switch (m_ColorAttachmentSpecifications[i].TextureFormat)
				{
				case FramebufferTextureFormat::RGBA8:
					Utils::AttachColorTexture(m_ColorAttachments[i], m_Specification.Samples,
						GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE,
						m_Specification.Width, m_Specification.Height, i);
					break;

				case FramebufferTextureFormat::RGBA16F:
					Utils::AttachColorTexture(m_ColorAttachments[i], m_Specification.Samples,
						GL_RGBA16F /* 可保留超过 1 的亮度 */, GL_RGBA, GL_FLOAT,
						m_Specification.Width, m_Specification.Height, i);
					break;

				case FramebufferTextureFormat::RG16F:
					Utils::AttachColorTexture(m_ColorAttachments[i], m_Specification.Samples,
						GL_RG16F, GL_RG, GL_FLOAT,
						m_Specification.Width, m_Specification.Height, i);
					break;

				case FramebufferTextureFormat::RED_INTEGER:
					Utils::AttachColorTexture(m_ColorAttachments[i], m_Specification.Samples,
						GL_R32I /* 每个像素存一个实体编号 */, GL_RED_INTEGER, GL_INT,
						m_Specification.Width, m_Specification.Height, i);
					break;
				}
			}
		}

		// ---- 深度附件 ----
		if (m_DepthAttachmentSpecifications.TextureFormat != FramebufferTextureFormat::None)
		{
			Utils::CreateTextures(multisample, &m_DepthAttachment, 1);
			Utils::BindTexture(multisample, m_DepthAttachment);
			switch (m_DepthAttachmentSpecifications.TextureFormat)
			{
			case FramebufferTextureFormat::DEPTH24STENCIL8:
				Utils::AttachDepthTexture(m_DepthAttachment, m_Specification.Samples,
					GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL_ATTACHMENT,
					m_Specification.Width, m_Specification.Height);
				break;

			case FramebufferTextureFormat::DEPTH32F:
				Utils::AttachDepthTexture(m_DepthAttachment, m_Specification.Samples,
					GL_DEPTH_COMPONENT32F, GL_DEPTH_ATTACHMENT,
					m_Specification.Width, m_Specification.Height);
				break;
			}
		}

		if (m_ColorAttachments.size() > 1)
		{
			RF_CORE_ASSERT(m_ColorAttachments.size() <= 4);
			GLenum buffers[4] = { GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1,GL_COLOR_ATTACHMENT2,GL_COLOR_ATTACHMENT3 };
			glDrawBuffers(m_ColorAttachments.size(), buffers);
		}
		else if(m_ColorAttachments.empty())
		{
			glDrawBuffer(GL_NONE /* 阴影图只写深度 */);
		}

		RF_CORE_ASSERT(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "framebuffer is not completed!");

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	void OpenGLFramebuffer::Bind()
	{
		glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);

		// 阴影图和编辑器视口的大小可能不同。切换绘制目标后，按这张图的宽高绘制。
		glViewport(0, 0, m_Specification.Width, m_Specification.Height);


	}

	void OpenGLFramebuffer::Unbind()
	{
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	void OpenGLFramebuffer::Resize(uint32_t width, uint32_t height)
	{
		m_Specification.Width = width;
		m_Specification.Height = height;

		Invalidate();
	}

	// ---- 实体拾取 ----
	int OpenGLFramebuffer::ReadPixel(uint32_t attachmentIndex, int x, int y)
	{
		RF_CORE_ASSERT(attachmentIndex < m_ColorAttachments.size());
		RF_CORE_ASSERT(m_ColorAttachmentSpecifications[attachmentIndex].TextureFormat == FramebufferTextureFormat::RED_INTEGER,
			"ReadPixel must read from RED_INTEGER attachment");

		glReadBuffer(GL_COLOR_ATTACHMENT0 + attachmentIndex);
		int pixelData;
		glReadPixels(x, y, 1, 1, GL_RED_INTEGER, GL_INT, &pixelData);
		return pixelData;
	}

	// ---- 清除附件 ----
	void OpenGLFramebuffer::ClearAttachment(uint32_t attachmentIndex, int value)
	{
		RF_CORE_ASSERT(attachmentIndex < m_ColorAttachments.size());

		auto& spec = m_ColorAttachmentSpecifications[attachmentIndex];

		GLenum format = Utils::RedflagFBTextureToGL(spec.TextureFormat);
		GLenum type   = Utils::GLDataType(spec.TextureFormat);

		if (type == GL_INT)
		{
			// 编辑器每帧先填 -1，鼠标点在背景上就知道没选中实体。
			glClearTexImage(m_ColorAttachments[attachmentIndex], 0, format, GL_INT, &value);
		}
		else
		{
			float fValue = static_cast<float>(value);
			float clearColor[4] = { fValue, fValue, fValue, fValue };
			glClearTexImage(m_ColorAttachments[attachmentIndex], 0, format, GL_FLOAT, clearColor);
		}
	}

	uint32_t OpenGLFramebuffer::GetColorAttachmentRendererID(uint32_t index) const
	{
		RF_CORE_ASSERT(index < m_ColorAttachments.size());
		return m_ColorAttachments[index];
	}

	uint32_t OpenGLFramebuffer::GetDepthAttachmentRendererID() const
	{
		return m_DepthAttachment;
	}
}