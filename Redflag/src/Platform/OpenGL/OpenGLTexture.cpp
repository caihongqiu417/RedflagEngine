#include"rfpch.h"
#include"OpenGLTexture.h"

#include"stb_image.h"

#define TINYEXR_IMPLEMENTATION
#include "tinyexr.h"



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

		// ---- EXR path (tinyexr) ----
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

			// Flip vertically: EXR top-to-bottom → OpenGL bottom-to-top
			for (int y = 0; y < exrHeight / 2; ++y)
			{
				int opp = exrHeight - 1 - y;
				for (int x = 0; x < exrWidth * 4; ++x)
					std::swap(exrData[y * exrWidth * 4 + x], exrData[opp * exrWidth * 4 + x]);
			}

			// Auto-detect [-1,1] normal map encoding vs standard [0,1].
			// Some DCC tools (Blender render etc.) export tangent-space normals
			// directly as floats in [-1,1].  The shader expects [0,1] (same as PNG
			// normal maps) and decodes with * 2.0 - 1.0.  If any R/G value is
			// negative we remap the whole image: v = v * 0.5 + 0.5.
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
			glTextureSubImage2D(m_RendererID, 0, 0, 0, m_Width, m_Height,
				GL_RGBA, GL_FLOAT, exrData);

			free(exrData);
			return;
		}

		// ---- Standard path (stb_image) ----
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
