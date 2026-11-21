#pragma once

#include"Redflag/Renderer/Texture.h"

#include<glad/glad.h>

namespace Redflag {
	/* 持有一张 OpenGL 2D 纹理。文件构造同步解码并上传，CPU 像素随后释放。 */
	class OpenGLTexture2D :public Texture2D
	{
	public:
		// ---- 创建与销毁 ----
		OpenGLTexture2D(uint32_t width, uint32_t height);
		OpenGLTexture2D(const std::string& path);
		virtual ~OpenGLTexture2D();

		// ---- 规格与对象查询 ----
		// RendererID 为借用的 OpenGL 纹理名，调用方不得释放。
		virtual uint32_t GetWidth() const override { return m_Width; }
		virtual uint32_t GetHeight() const override { return m_Height; }
		virtual uint32_t GetRendererID() const override { return m_RendererID; }

		// ---- 上传与采样 ----
		// SetData 要求整张图的紧密 8 位像素，通道数与创建时一致，size 为字节数。
		virtual void SetData(void* data, uint32_t size) override; 

		virtual void Bind(uint32_t slot=0 /* Shader 纹理单元 */) const override;

		virtual bool operator==(const Texture& other) const override
		{
			return m_RendererID == ((OpenGLTexture2D&)other).m_RendererID;
		}

		virtual const std::string& GetPath() const override { return m_Path; }
	private:
		std::string m_Path;
		uint32_t m_Width, m_Height;
		uint32_t m_RendererID;
		GLenum m_InternalFormat, m_DataFormat;
	};
}
