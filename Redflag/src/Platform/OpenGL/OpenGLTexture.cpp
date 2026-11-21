#include"rfpch.h"
#include"OpenGLTexture.h"

#include"stb_image.h"

#define TINYEXR_IMPLEMENTATION
#include "tinyexr.h"



namespace Redflag {
	// 文件解码与 SetData 都提供紧密排列的像素，不含行末填充字节。
	static void UploadPixels(uint32_t texture, uint32_t width, uint32_t height,
		GLenum format, GLenum type, const void* pixels)
	{
		GLint alignment;
		glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTextureSubImage2D(texture, 0, 0, 0, width, height, format, type, pixels);
		glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
	}

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

		// ---- EXR 浮点像素 ----
		bool isEXR = path.size() > 4 &&
			(path.compare(path.size() - 4, 4, ".exr") == 0 ||
			 path.compare(path.size() - 4, 4, ".EXR") == 0);

		if (isEXR)
		{
			float* exrData = nullptr;
			int exrWidth = 0, exrHeight = 0;
			const char* err = nullptr;
			int ret = LoadEXR(&exrData, &exrWidth, &exrHeight, path.c_str(), &err);
			if (ret != TINYEXR_SUCCESS)
			{
				if (err) { RF_CORE_ERROR("EXR load failed ({0}): {1}", path, err); FreeEXRErrorMessage(err); }
				else     { RF_CORE_ERROR("EXR load failed: {0}", path); }
				m_Width = m_Height = 1; m_InternalFormat = GL_RGBA16F; m_DataFormat = GL_RGBA;
				m_RendererID = 0;
				return;
			}

			// 将文件从上到下的行序翻转，与本项目纹理 UV 的下方起点一致。
			for (int y = 0; y < exrHeight / 2; ++y)
			{
				int opp = exrHeight - 1 - y;
				for (int x = 0; x < exrWidth * 4; ++x)
					std::swap(exrData[y * exrWidth * 4 + x], exrData[opp * exrWidth * 4 + x]);
			}

			// 兼容部分法线 EXR：前 2048 像素的 R/G 存在负值时，按 [-1,1]→[0,1] 映射。
			// 这是数据启发式判断，当前作用于所有通道，不能识别贴图的真实用途。
			{
				bool needsRemap = false;
				int samplePx = std::min(exrWidth * exrHeight, 2048);
				for (int i = 0; i < samplePx && !needsRemap; ++i)
				{
					if (exrData[i * 4 + 0] < -0.01f || exrData[i * 4 + 1] < -0.01f)
						needsRemap = true;
				}
				if (needsRemap)
				{
					int total = exrWidth * exrHeight * 4;
					for (int i = 0; i < total; ++i)
						exrData[i] = exrData[i] * 0.5f + 0.5f;
					RF_CORE_INFO("EXR '{0}': remapped [-1,1] normals to [0,1]", path);
				}
			}

			m_Width  = (uint32_t)exrWidth;
			m_Height = (uint32_t)exrHeight;
			m_InternalFormat = GL_RGBA16F;
			m_DataFormat     = GL_RGBA;

			glCreateTextures(GL_TEXTURE_2D, 1, &m_RendererID);
			glTextureStorage2D(m_RendererID, 1, GL_RGBA16F, m_Width, m_Height);
			glTextureParameteri(m_RendererID, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTextureParameteri(m_RendererID, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_S, GL_REPEAT);
			glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_T, GL_REPEAT);
			UploadPixels(m_RendererID, m_Width, m_Height, GL_RGBA, GL_FLOAT, exrData);

			free(exrData);
			return;
		}

		// ---- 灰度、灰度透明度、RGB、RGBA 8 位像素 ----
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

		// stb 的通道数固定为 1~4；灰度保留单通道存储。
		const GLenum internalFormats[] = { GL_R8, GL_RG8, GL_RGB8, GL_RGBA8 };
		const GLenum dataFormats[] = { GL_RED, GL_RG, GL_RGB, GL_RGBA };
		m_InternalFormat = internalFormats[channels - 1];
		m_DataFormat = dataFormats[channels - 1];

		glCreateTextures(GL_TEXTURE_2D, 1, &m_RendererID);


		glTextureStorage2D(m_RendererID, 1, m_InternalFormat, m_Width, m_Height);


		glTextureParameteri(m_RendererID, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTextureParameteri(m_RendererID, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

		// UV 超出 [0,1] 时重复纹理，支持材质平铺。
		glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_T, GL_REPEAT);

		// 灰度作为颜色采样时复制到 RGB；双通道的第二分量保留为 alpha。
		if (channels <= 2)
		{
			const GLint swizzle[] = { GL_RED, GL_RED, GL_RED, channels == 2 ? GL_GREEN : GL_ONE };
			glTextureParameteriv(m_RendererID, GL_TEXTURE_SWIZZLE_RGBA, swizzle);
		}
		UploadPixels(m_RendererID, m_Width, m_Height, m_DataFormat, GL_UNSIGNED_BYTE, data);


		stbi_image_free(data);
	}


	OpenGLTexture2D::~OpenGLTexture2D()
	{
		RF_PROFILE_FUNCTION();

		glDeleteTextures(1, &m_RendererID);
	}


	// ---- 全图更新 ----
	// 此入口固定按 unsigned byte 上传；EXR 的 float 上传在文件构造中完成。
	void OpenGLTexture2D::SetData(void* data, uint32_t size)
	{
		RF_PROFILE_FUNCTION();

		uint32_t bpp = m_DataFormat == GL_RGBA ? 4 : m_DataFormat == GL_RGB ? 3 : m_DataFormat == GL_RG ? 2 : 1;
		RF_CORE_ASSERT(size==m_Width*m_Height*bpp,"Data must be entire texture!");
		UploadPixels(m_RendererID, m_Width, m_Height, m_DataFormat, GL_UNSIGNED_BYTE, data);
	}

	void OpenGLTexture2D::Bind(uint32_t slot) const
	{
		RF_PROFILE_FUNCTION();

		glBindTextureUnit(slot, m_RendererID);
	}
}
