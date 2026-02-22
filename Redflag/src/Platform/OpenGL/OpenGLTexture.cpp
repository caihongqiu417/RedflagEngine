#include"rfpch.h"
#include"OpenGLTexture.h"

#include"stb_image.h"



namespace Redflag {
	OpenGLTexture2D::OpenGLTexture2D(uint32_t width, uint32_t height)
		:m_Width(width), m_Height(height)
	{
		RF_PROFILE_FUNCTION();

		m_InternalFormat = GL_RGBA8;
		m_DataFormat = GL_RGBA;

		glCreateTextures(GL_TEXTURE_2D, 1, &m_RendererID);
		glTextureStorage2D(m_RendererID, 1, m_InternalFormat, m_Width, m_Height);

		glTextureParameteri(m_RendererID, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTextureParameteri(m_RendererID, GL_TEXTURE_MAG_FILTER, GL_NEAREST); //用于像素

		glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_T, GL_REPEAT);
	}

	OpenGLTexture2D::OpenGLTexture2D(const std::string& path)
		:m_Path(path)
	{
		RF_PROFILE_FUNCTION();

		int width, height, channels;
		stbi_uc* data = nullptr;

		stbi_set_flip_vertically_on_load(1); 
		{
			RF_PROFILE_SCOPE("stbi_load - OpenGLTexture2D::OpenGLTexture2D(const std::string&)");
			data = stbi_load(path.c_str(), &width, &height, &channels, 0);
		}
		
		RF_CORE_ASSERT(data, "Failed to load image!");
		m_Width = width;
		m_Height = height;

		// internalFormat：我们怎么存储这个图像    dataFormat：告诉显存提交给它的格式
		GLenum internalFormat = 0, dataFormat = 0;
		if(channels==4)
		{
			internalFormat = GL_RGBA8;
			dataFormat = GL_RGBA;
		}
		else if(channels==3)
		{
			internalFormat = GL_RGB8;
			dataFormat = GL_RGB;
		}
		m_InternalFormat = internalFormat;
		m_DataFormat = dataFormat;

		RF_CORE_ASSERT(internalFormat & dataFormat, "Format not supported!");

		glCreateTextures(GL_TEXTURE_2D, 1, &m_RendererID);

	
		glTextureStorage2D(m_RendererID, 1, internalFormat, m_Width, m_Height);


		glTextureParameteri(m_RendererID, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTextureParameteri(m_RendererID, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		
		// 设置纹理wrap模式，避免NPOT纹理的边缘问题
		glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_T, GL_REPEAT);

		glTextureSubImage2D(m_RendererID, 0, 0, 0, m_Width, m_Height,
			dataFormat, GL_UNSIGNED_BYTE, data);


		stbi_image_free(data);
	}


	OpenGLTexture2D::~OpenGLTexture2D()
	{
		RF_PROFILE_FUNCTION();

		glDeleteTextures(1, &m_RendererID);
	}


	void OpenGLTexture2D::SetData(void* data, uint32_t size)
	{
		RF_PROFILE_FUNCTION();

		uint32_t bpp = m_DataFormat == GL_RGBA ? 4 : 3;
		RF_CORE_ASSERT(size==m_Width*m_Height*bpp,"Data must be entire texture!");
		glTextureSubImage2D(m_RendererID, 0, 0, 0, m_Width, m_Height,m_DataFormat, GL_UNSIGNED_BYTE, data);
	}

	void OpenGLTexture2D::Bind(uint32_t slot) const
	{
		RF_PROFILE_FUNCTION();

		glBindTextureUnit(slot, m_RendererID);
	}
}
