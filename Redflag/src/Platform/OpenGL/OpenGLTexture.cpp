#include"rfpch.h"
#include"OpenGLTexture.h"

#include"stb_image.h"

#include<glad/glad.h>

namespace Redflag {
	OpenGLTexture2D::OpenGLTexture2D(const std::string& path)
		:m_Path(path)
	{
		int width, height, channels;
		stbi_set_flip_vertically_on_load(1); // OpenGL的纹理坐标系与图片的坐标系是相反的，加载图片时需要翻转Y轴
		stbi_uc* data = stbi_load(path.c_str(), &width, &height, &channels, 0);
		RF_CORE_ASSERT(data, "Failed to load image!");
		m_Width = width;
		m_Height = height;

		// 第一步：创建纹理对象（此时无存储空间）
		glCreateTextures(GL_TEXTURE_2D, 1, &m_RendererID);

		// 第二步：分配GPU存储空间并指定格式
		glTextureStorage2D(m_RendererID, 1, GL_RGB8, m_Width, m_Height);

		// 设置纹理采样时的过滤方式
		glTextureParameteri(m_RendererID, GL_TEXTURE_MIN_FILTER, GL_LINEAR);  
		glTextureParameteri(m_RendererID, GL_TEXTURE_MAG_FILTER, GL_NEAREST);  

		// 上传图片数据到GPU纹理
		glTextureSubImage2D(m_RendererID, 0, 0, 0, m_Width, m_Height,
			GL_RGB, GL_UNSIGNED_BYTE, data);

		// 释放CPU端图片数据
		stbi_image_free(data);
	}


	OpenGLTexture2D::~OpenGLTexture2D()
	{
		glDeleteTextures(1, &m_RendererID);
	}

	void OpenGLTexture2D::Bind(uint32_t slot) const
	{
		glBindTextureUnit(slot, m_RendererID);
	}
}
