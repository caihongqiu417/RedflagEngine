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

				// 整数格式（如 Entity ID）必须用 NEAREST，否则插值会产生无效值。
				// 浮点格式（RGBA8、RGBA16F 等）用 LINEAR 获得平滑采样。
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

				// 深度纹理用 NEAREST 更安全：
				//   - Shadow Map 采样时避免在深度不连续处插值出错误的遮挡判断
				//   - 如果后面要做 PCF 软阴影，会在 shader 里手动采多次，不需要硬件插值
				// 如果你以后想做 VSM（方差阴影）等需要 LINEAR 的方案，再改这里
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			}
			glFramebufferTexture2D(GL_FRAMEBUFFER, attachmentType, TextureTarget(multisampled), id, 0);
		}

		// ---------------------------------------------------------------
		// IsDepthFormat — 判断一个格式是否属于深度类格式
		//
		// 这个函数在构造函数中用于把 attachment 规格分成
		// 「颜色附件」和「深度附件」两类。
		// 新增格式时必须同步更新这里。
		// ---------------------------------------------------------------
		static bool IsDepthFormat(FramebufferTextureFormat format)
		{
			switch (format)
			{
			case FramebufferTextureFormat::DEPTH24STENCIL8: return true;
			case FramebufferTextureFormat::DEPTH32F:        return true;
			}
			return false;
		}

		// ---------------------------------------------------------------
		// RedflagFBTextureToGL — 获取 glClearTexImage 等接口需要的
		//                        「外部像素格式」（format 参数）
		//
		// 注意区分三个概念：
		//   internalFormat — GPU 内部存储格式（如 GL_RGBA16F）
		//   format         — CPU → GPU 传输时的像素布局（如 GL_RGBA）
		//   type           — CPU 侧每分量的数据类型（如 GL_FLOAT）
		//
		// 这个函数返回的是 format。
		// ---------------------------------------------------------------
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

		// ---------------------------------------------------------------
		// GLDataType — 获取 format 对应的 CPU 侧数据类型
		//
		// 用途：glClearTexImage 的 type 参数、glTexImage2D 的 type 参数。
		// 关键点：
		//   - RED_INTEGER 用 GL_INT（对应 R32I）
		//   - RGBA8 用 GL_UNSIGNED_BYTE
		//   - RGBA16F / RG16F 用 GL_FLOAT（half 精度上传也用 float，驱动会转换）
		// ---------------------------------------------------------------
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

	void OpenGLFramebuffer::Invalidate()
	{
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

		//Attachment
		if (m_ColorAttachmentSpecifications.size())
		{
			m_ColorAttachments.resize(m_ColorAttachmentSpecifications.size());
			Utils::CreateTextures(multisample, m_ColorAttachments.data(), m_ColorAttachments.size());

			for (size_t i = 0;i < m_ColorAttachments.size(); i++)
			{
				Utils::BindTexture(multisample, m_ColorAttachments[i]);
				switch (m_ColorAttachmentSpecifications[i].TextureFormat)
				{
				// --- LDR 颜色（8-bit）---
				// internalFormat = GL_RGBA8  → GPU 内部每通道 8 位无符号整数
				// format         = GL_RGBA   → 4 通道像素布局
				// type           = GL_UNSIGNED_BYTE
				case FramebufferTextureFormat::RGBA8:
					Utils::AttachColorTexture(m_ColorAttachments[i], m_Specification.Samples,
						GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE,
						m_Specification.Width, m_Specification.Height, i);
					break;

				// --- HDR 颜色（16-bit 浮点）---
				// internalFormat = GL_RGBA16F → GPU 内部每通道 16 位半精度浮点
				// format         = GL_RGBA    → 4 通道
				// type           = GL_FLOAT   → CPU 侧用 float 上传，驱动自动截断为 half
				//
				// 为什么选 RGBA16F 而不是 RGBA32F？
				//   带宽减半，精度对 HDR 场景颜色足够。RGBA32F 通常只用于计算 buffer。
				case FramebufferTextureFormat::RGBA16F:
					Utils::AttachColorTexture(m_ColorAttachments[i], m_Specification.Samples,
						GL_RGBA16F, GL_RGBA, GL_FLOAT,
						m_Specification.Width, m_Specification.Height, i);
					break;

				// --- 双通道 16-bit 浮点 ---
				// 用于法线压缩（xy 两分量，z 可由 sqrt(1-x²-y²) 重建）
				// 或运动向量（motion vector）等只需要 2 通道的场景
				case FramebufferTextureFormat::RG16F:
					Utils::AttachColorTexture(m_ColorAttachments[i], m_Specification.Samples,
						GL_RG16F, GL_RG, GL_FLOAT,
						m_Specification.Width, m_Specification.Height, i);
					break;

				// --- 整数单通道（Entity ID 拾取）---
				// internalFormat = GL_R32I      → 32 位有符号整数
				// format         = GL_RED_INTEGER
				// type           = GL_INT
				case FramebufferTextureFormat::RED_INTEGER:
					Utils::AttachColorTexture(m_ColorAttachments[i], m_Specification.Samples,
						GL_R32I, GL_RED_INTEGER, GL_INT,
						m_Specification.Width, m_Specification.Height, i);
					break;
				}
			}
		}

		if (m_DepthAttachmentSpecifications.TextureFormat != FramebufferTextureFormat::None)
		{
			Utils::CreateTextures(multisample, &m_DepthAttachment, 1);
			Utils::BindTexture(multisample, m_DepthAttachment);
			switch (m_DepthAttachmentSpecifications.TextureFormat)
			{
			// 24-bit 深度 + 8-bit 模板，最常用的组合。
			// attachmentType = GL_DEPTH_STENCIL_ATTACHMENT 表示同时绑定深度和模板。
			case FramebufferTextureFormat::DEPTH24STENCIL8:
				Utils::AttachDepthTexture(m_DepthAttachment, m_Specification.Samples,
					GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL_ATTACHMENT,
					m_Specification.Width, m_Specification.Height);
				break;

			// 32-bit 浮点纯深度，无模板。
			// 比 DEPTH24STENCIL8 多 8 位精度，且 shader 中可以直接作为
			// sampler2D 采样（Shadow Map 的核心需求）。
			// attachmentType = GL_DEPTH_ATTACHMENT（只绑深度，不绑模板）。
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
			glDrawBuffer(GL_NONE);
		}

		// === ���֡������������ ===
		RF_CORE_ASSERT(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "framebuffer is not completed!");

		// === ���֡������ ===
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	void OpenGLFramebuffer::Bind()
	{
		glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);

		// ȷ����Ⱦ��ͼ�������֡��������Сһ�£����������ü�
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

	int OpenGLFramebuffer::ReadPixel(uint32_t attachmentIndex, int x, int y)
	{
		RF_CORE_ASSERT(attachmentIndex < m_ColorAttachments.size());
		RF_CORE_ASSERT(m_ColorAttachmentSpecifications[attachmentIndex].TextureFormat == FramebufferTextureFormat::RED_INTEGER,
			"ReadPixel must read from RED_INTEGER attachment");

		glReadBuffer(GL_COLOR_ATTACHMENT0 + attachmentIndex);
		int pixelData;
		glReadPixels(x, y, 
			1, 1,
			GL_RED_INTEGER//��ȡ��ɫͨ��
			, GL_INT, // ת��������
			&pixelData);
		return pixelData;
	}

	// ---------------------------------------------------------------
	// ClearAttachment — 将指定颜色附件清为给定值
	//
	// 修复说明（相对于旧版本）：
	//   旧版本统一用 GL_INT 作为 type 参数，这对 RGBA8 / RGBA16F 等
	//   浮点/归一化格式是错误的。glClearTexImage 要求 type 与内部格式
	//   兼容，否则行为未定义。
	//
	// 现在的做法：
	//   - RED_INTEGER (R32I) → GL_INT + int value（与旧行为一致）
	//   - RGBA8              → GL_FLOAT + float[4]（GL 会自动钳位到 [0,1]）
	//   - RGBA16F / RG16F   → GL_FLOAT + float[4]（直接写浮点值）
	//
	// 参数 value 对整数格式仍然是 entity ID 等整数；
	// 对浮点格式，这里统一清为 0.0f（黑色透明）。
	// 如果后续需要按 float 值清浮点 attachment，可以新增重载。
	// ---------------------------------------------------------------
	void OpenGLFramebuffer::ClearAttachment(uint32_t attachmentIndex, int value)
	{
		RF_CORE_ASSERT(attachmentIndex < m_ColorAttachments.size());

		auto& spec = m_ColorAttachmentSpecifications[attachmentIndex];

		GLenum format = Utils::RedflagFBTextureToGL(spec.TextureFormat);
		GLenum type   = Utils::GLDataType(spec.TextureFormat);

		if (type == GL_INT)
		{
			// 整数格式：直接把 int value 写入（Entity ID 清为 -1 等场景）
			glClearTexImage(m_ColorAttachments[attachmentIndex], 0, format, GL_INT, &value);
		}
		else
		{
			// 浮点 / 归一化格式：把 int value 重新解释为 float 进行清除
			// 常见用法：value=0 → 清为黑色（0.0f）
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

	// ---------------------------------------------------------------
	// GetDepthAttachmentRendererID — 返回深度附件的 GL 纹理 ID
	//
	// 用途：
	//   1. Shadow Map Pass 把场景深度写入一个 depth-only FBO，
	//      然后主渲染 Pass 需要绑定这个深度纹理作为 sampler2DShadow。
	//   2. SSAO 需要读取场景深度来计算遮蔽。
	//   3. 编辑器调试视图中显示 Depth buffer。
	//
	// 如果 FBO 没有深度附件（m_DepthAttachment == 0），返回 0，
	// 调用方应该检查返回值。
	// ---------------------------------------------------------------
	uint32_t OpenGLFramebuffer::GetDepthAttachmentRendererID() const
	{
		return m_DepthAttachment;
	}
}