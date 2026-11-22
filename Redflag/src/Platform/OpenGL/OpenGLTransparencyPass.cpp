#include "rfpch.h"
#include "OpenGLTransparencyPass.h"
#include <glad/glad.h>

namespace Redflag
{
	static GLuint MakeAttachment(int width, int height, GLenum format, GLenum filter = GL_NEAREST)
	{
		GLuint texture;
		glCreateTextures(GL_TEXTURE_2D, 1, &texture);
		glTextureStorage2D(texture, 1, format, width, height);
		glTextureParameteri(texture, GL_TEXTURE_MIN_FILTER, filter);
		glTextureParameteri(texture, GL_TEXTURE_MAG_FILTER, filter);
		glTextureParameteri(texture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTextureParameteri(texture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		return texture;
	}

	// 玻璃沿用实心物体的深度做遮挡。复制深度时，即使目标改为单采样，两张深度图也要用同一格式。
	static GLenum DepthFormat(GLuint framebuffer)
	{
		if (framebuffer)
		{
			GLint type, object, format;
			glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &type);
			if (type == GL_NONE) return GL_NONE;
			glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &object);
			if (type == GL_TEXTURE) glGetTextureLevelParameteriv(object, 0, GL_TEXTURE_INTERNAL_FORMAT, &format);
			else glGetNamedRenderbufferParameteriv(object, GL_RENDERBUFFER_INTERNAL_FORMAT, &format);
			return format;
		}

		GLint depth, stencil;
		glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, GL_DEPTH, GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE, &depth);
		glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, GL_STENCIL, GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE, &stencil);
		if (stencil) return depth == 32 ? GL_DEPTH32F_STENCIL8 : GL_DEPTH24_STENCIL8;
		if (depth == 16) return GL_DEPTH_COMPONENT16;
		if (depth == 24) return GL_DEPTH_COMPONENT24;
		return depth ? GL_DEPTH_COMPONENT32F : GL_NONE;
	}

	OpenGLTransparencyPass::OpenGLTransparencyPass()
	{
		glCreateFramebuffers(1, &m_Target);
		glCreateFramebuffers(1, &m_Snapshot);
		const GLenum outputs[] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
		glNamedFramebufferDrawBuffers(m_Target, 3, outputs);
		glCreateVertexArrays(1, &m_VertexArray);
		m_Composite = Shader::Create("assets/shaders/Transparency_Composite.glsl");
		GLint drawBuffers;
		glGetIntegerv(GL_MAX_DRAW_BUFFERS, &drawBuffers);
		m_DrawBuffers.resize(drawBuffers);
	}

	OpenGLTransparencyPass::~OpenGLTransparencyPass()
	{
		glDeleteTextures(3, m_Layers.data());
		glDeleteTextures(1, &m_Background);
		glDeleteTextures(1, &m_Depth);
		glDeleteFramebuffers(1, &m_Target);
		glDeleteFramebuffers(1, &m_Snapshot);
		glDeleteVertexArrays(1, &m_VertexArray);
	}

	void OpenGLTransparencyPass::Resize(int width, int height, int depthFormat)
	{
		if (m_Width == width && m_Height == height && m_DepthFormat == depthFormat)
			return;

		glDeleteTextures(3, m_Layers.data());
		glDeleteTextures(1, &m_Background);
		glDeleteTextures(1, &m_Depth);
		m_Width = width;
		m_Height = height;
		m_DepthFormat = depthFormat;
		m_Background = MakeAttachment(width, height, GL_RGBA16F, GL_LINEAR);
		m_Depth = MakeAttachment(width, height, depthFormat ? depthFormat : GL_DEPTH_COMPONENT32F);
		glNamedFramebufferTexture(m_Snapshot, GL_COLOR_ATTACHMENT0, m_Background, 0);
		glNamedFramebufferTexture(m_Snapshot, GL_DEPTH_ATTACHMENT, m_Depth, 0);
		glNamedFramebufferTexture(m_Target, GL_DEPTH_ATTACHMENT, m_Depth, 0);
		for (int i = 0; i < 3; ++i)
		{
			m_Layers[i] = MakeAttachment(width, height, GL_RGBA32F);
			glNamedFramebufferTexture(m_Target, GL_COLOR_ATTACHMENT0 + i, m_Layers[i], 0);
		}
	}

	void OpenGLTransparencyPass::Begin()
	{
		glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &m_SceneTarget);
		glGetIntegerv(GL_VIEWPORT, m_Viewport);
		Resize(m_Viewport[2], m_Viewport[3], DepthFormat(m_SceneTarget));
		glGetIntegerv(GL_DEPTH_FUNC, &m_DepthFunc);
		glGetBooleanv(GL_DEPTH_WRITEMASK, &m_DepthWrite);
		m_DepthTest = glIsEnabled(GL_DEPTH_TEST);
		m_CullFace = glIsEnabled(GL_CULL_FACE);
		m_Scissor = glIsEnabled(GL_SCISSOR_TEST);
		glDisable(GL_SCISSOR_TEST);
		glGetIntegerv(GL_CULL_FACE_MODE, &m_CullMode);
		for (GLuint i = 0; i < m_DrawBuffers.size(); ++i)
		{
			GLint buffer;
			glGetIntegerv(GL_DRAW_BUFFER0 + i, &buffer);
			m_DrawBuffers[i] = buffer;
		}

		// 拾取可能刚读过实体编号。这里专门选择主画面的颜色，并保留原来的读取目标。
		GLint readTarget, readBuffer;
		glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readTarget);
		glBindFramebuffer(GL_READ_FRAMEBUFFER, m_SceneTarget);
		glGetIntegerv(GL_READ_BUFFER, &readBuffer);
		glReadBuffer(m_DrawBuffers[0]);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_Snapshot);
		glBlitFramebuffer(m_Viewport[0], m_Viewport[1], m_Viewport[0] + m_Width, m_Viewport[1] + m_Height,
			0, 0, m_Width, m_Height, GL_COLOR_BUFFER_BIT | (m_DepthFormat ? GL_DEPTH_BUFFER_BIT : 0), GL_NEAREST);
		glReadBuffer(readBuffer);
		glBindFramebuffer(GL_READ_FRAMEBUFFER, readTarget);

		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_Target);
		glViewport(0, 0, m_Width, m_Height);
		if (!m_DepthFormat)
		{
			const GLfloat farDepth = 1.0f;
			glDepthMask(GL_TRUE);
			glClearBufferfv(GL_DEPTH, 0, &farDepth);
		}
		glDepthMask(GL_FALSE);
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LEQUAL);
		glCullFace(GL_BACK);

		const GLfloat zero[] = { 0, 0, 0, 0 };
		for (GLuint i = 0; i < 3; ++i)
		{
			auto& color = m_ColorState[i];
			color.Blend = glIsEnabledi(GL_BLEND, i);
			glGetBooleani_v(GL_COLOR_WRITEMASK, i, color.Write);
			glGetIntegeri_v(GL_BLEND_SRC_RGB, i, &color.SourceRGB);
			glGetIntegeri_v(GL_BLEND_DST_RGB, i, &color.DestRGB);
			glGetIntegeri_v(GL_BLEND_SRC_ALPHA, i, &color.SourceAlpha);
			glGetIntegeri_v(GL_BLEND_DST_ALPHA, i, &color.DestAlpha);
			glGetIntegeri_v(GL_BLEND_EQUATION_RGB, i, &color.EquationRGB);
			glGetIntegeri_v(GL_BLEND_EQUATION_ALPHA, i, &color.EquationAlpha);
			glColorMaski(i, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
			glClearBufferfv(GL_COLOR, i, zero);
			glEnablei(GL_BLEND, i);
			glBlendEquationi(i, GL_FUNC_ADD);
			glBlendFunci(i, GL_ONE, GL_ONE);
		}
	}

	void OpenGLTransparencyPass::Composite()
	{
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_SceneTarget);
		glViewport(m_Viewport[0], m_Viewport[1], m_Width, m_Height);
		if (m_Scissor) glEnable(GL_SCISSOR_TEST);
		glDrawBuffer(m_DrawBuffers[0]); // 保留主画面里的实体编号和法线，只改颜色。
		glDisablei(GL_BLEND, 0);
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_CULL_FACE);
		glBindTextureUnit(0, m_Background);
		glBindTextures(1, 3, m_Layers.data());
		glBindTextureUnit(4, m_Depth);
		m_Composite->Bind();
		glBindVertexArray(m_VertexArray);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glBindVertexArray(0);

		if (m_SceneTarget) glDrawBuffers(static_cast<GLsizei>(m_DrawBuffers.size()), m_DrawBuffers.data());
		for (GLuint i = 0; i < 3; ++i)
		{
			const auto& color = m_ColorState[i];
			glColorMaski(i, color.Write[0], color.Write[1], color.Write[2], color.Write[3]);
			glBlendEquationSeparatei(i, color.EquationRGB, color.EquationAlpha);
			glBlendFuncSeparatei(i, color.SourceRGB, color.DestRGB, color.SourceAlpha, color.DestAlpha);
			if (color.Blend) glEnablei(GL_BLEND, i); else glDisablei(GL_BLEND, i);
		}
		glDepthMask(m_DepthWrite);
		glDepthFunc(m_DepthFunc);
		if (m_DepthTest) glEnable(GL_DEPTH_TEST);
		glCullFace(m_CullMode);
		if (m_CullFace) glEnable(GL_CULL_FACE);
	}
}
