#pragma once

#include"Redflag/Renderer/Texture.h"

#include<glad/glad.h>

namespace Redflag {
	/* 这个对象持有一张 OpenGL 贴图，析构时释放。从文件加载时，构造函数读出像素并上传给显卡。 */
	class OpenGLTexture2D :public Texture2D
	{
	public:
		// ---- 创建与销毁 ----
		OpenGLTexture2D(uint32_t width, uint32_t height);
		OpenGLTexture2D(const std::string& path);
		virtual ~OpenGLTexture2D();

		// ---- 规格与对象查询 ----
		// GetRendererID 返回这张贴图在 OpenGL 里的编号。贴图由这个对象释放。
		virtual uint32_t GetWidth() const override { return m_Width; }
		virtual uint32_t GetHeight() const override { return m_Height; }
		virtual uint32_t GetRendererID() const override { return m_RendererID; }

		// ---- 上传与采样 ----
		// 传入整张图的像素，通道数与创建时相同。每个通道占一字节，行与行紧挨着，size 是总字节数。
		virtual void SetData(void* data, uint32_t size) override; 

		virtual void Bind(uint32_t slot=0 /* Shader 纹理单元 */) const override;
		// 发光贴图与数据贴图可共用像素。这个入口在过滤前把 sRGB 换成线性亮度。
		void BindSRGB(uint32_t slot);

		virtual bool operator==(const Texture& other) const override
		{
			return m_RendererID == ((OpenGLTexture2D&)other).m_RendererID;
		}

		virtual const std::string& GetPath() const override { return m_Path; }
	private:
		std::string m_Path;
		uint32_t m_Width, m_Height;
		uint32_t m_RendererID;
		uint32_t m_SRGBView = 0;
		GLenum m_InternalFormat, m_DataFormat;
	};
}
