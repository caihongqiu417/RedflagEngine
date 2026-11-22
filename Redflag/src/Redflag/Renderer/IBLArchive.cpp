#include "rfpch.h"
#include "IBLArchive.h"
#include "IBLPrecompute.h"
#include "Redflag/Core/UUID.h"

#include <glad/glad.h>
#include <array>
#include <fstream>
#include <glm/gtc/packing.hpp>

namespace Redflag::IBLArchive
{
	namespace
	{
		// 第一版文件只存着色时会读取的数据。尺寸或排列改变时，另用新版本。
		constexpr std::array<char, 8> Signature{ 'R','F','I','B','L','0','1','\n' };
		struct ImageLayout
		{
			uint32_t IBLTextures::*Texture;
			int Size, Levels, Faces;
			GLenum Format;

			size_t FaceValues(int level) const
			{
				const size_t width = Size >> level;
				return width * width * (Format == GL_RGB ? 3 : 2);
			}
		};

		constexpr ImageLayout Images[] = {
			{ &IBLTextures::EnvCubemap, IBLTextures::SkySize, 1, 6, GL_RGB },
			{ &IBLTextures::IrradianceMap, IBLTextures::DiffuseSize, 1, 6, GL_RGB },
			{ &IBLTextures::PrefilterMap, IBLTextures::ReflectionSize, IBLTextures::ReflectionLevels, 6, GL_RGB },
			{ &IBLTextures::BRDFLUT, IBLTextures::LutSize, 1, 1, GL_RG }
		};

		std::streamoff FileSize()
		{
			size_t bytes = Signature.size();
			for (const ImageLayout& image : Images)
				for (int level = 0; level < image.Levels; ++level)
					bytes += image.FaceValues(level) * image.Faces * sizeof(uint16_t);
			return static_cast<std::streamoff>(bytes);
		}

		bool ReadHeader(std::ifstream& file)
		{
			if (!file || file.tellg() != FileSize()) return false;
			file.seekg(0);
			std::array<char, Signature.size()> signature{};
			file.read(signature.data(), signature.size());
			return signature == Signature;
		}

		GLuint CreateImage(const ImageLayout& image)
		{
			GLuint texture;
			const bool cube = image.Faces == 6;
			glCreateTextures(cube ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D, 1, &texture);
			glTextureStorage2D(texture, image.Levels, cube ? GL_RGB16F : GL_RG16F, image.Size, image.Size);
			glTextureParameteri(texture, GL_TEXTURE_MIN_FILTER, image.Levels > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
			glTextureParameteri(texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTextureParameteri(texture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTextureParameteri(texture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			if (cube) glTextureParameteri(texture, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
			return texture;
		}
	}

	bool Save(const std::filesystem::path& path, const IBLTextures& textures)
	{
		// 写完再替换，保存中途磁盘写满也能继续用上一次的烘焙。
		std::filesystem::path temporary = path;
		temporary += "." + std::to_string(uint64_t(UUID())) + ".tmp";
		std::ofstream file(temporary, std::ios::binary);
		if (!file) return false;
		file.write(Signature.data(), Signature.size());

		std::vector<uint16_t> pixels;
		for (const ImageLayout& image : Images)
		{
			for (int level = 0; level < image.Levels && file; ++level)
			{
				const int width = image.Size >> level;
				pixels.resize(image.FaceValues(level));
				const GLsizei bytes = static_cast<GLsizei>(pixels.size() * sizeof(uint16_t));
				for (int face = 0; face < image.Faces && file; ++face)
				{
					glGetTextureSubImage(textures.*image.Texture, level, 0, 0, face, width, width, 1,
						image.Format, GL_HALF_FLOAT, bytes, pixels.data());
					file.write(reinterpret_cast<const char*>(pixels.data()), bytes);
				}
			}
		}

		file.close();
		std::error_code error;
		if (file)
		{
			std::filesystem::rename(temporary, path, error);
			if (!error) return true;
		}
		std::filesystem::remove(temporary, error);
		return false;
	}

	bool Load(const std::filesystem::path& path, IBLTextures& textures)
	{
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		if (!ReadHeader(file)) return false;

		IBLTextures loaded;
		std::vector<uint16_t> pixels;
		for (const ImageLayout& image : Images)
		{
			GLuint texture = CreateImage(image);
			loaded.*image.Texture = texture;
			for (int level = 0; level < image.Levels; ++level)
			{
				const int width = image.Size >> level;
				pixels.resize(image.FaceValues(level));
				for (int face = 0; face < image.Faces; ++face)
				{
					if (!file.read(reinterpret_cast<char*>(pixels.data()), pixels.size() * sizeof(uint16_t)))
					{
						for (const ImageLayout& entry : Images)
							glDeleteTextures(1, &(loaded.*entry.Texture));
						return false;
					}
					if (image.Faces == 6)
						glTextureSubImage3D(texture, level, 0, 0, face, width, width, 1, image.Format, GL_HALF_FLOAT, pixels.data());
					else
						glTextureSubImage2D(texture, level, 0, 0, width, width, image.Format, GL_HALF_FLOAT, pixels.data());
				}
			}
		}
		textures = loaded;
		return true;
	}

	bool ReadLighting(const std::filesystem::path& path, std::vector<glm::vec4>& pixels)
	{
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		if (!ReadHeader(file)) return false;
		// 天空背景的六个 2048² 大面不参加材质光照，直接跳到漫反射数据。
		file.seekg(Images[0].FaceValues(0) * 6 * sizeof(uint16_t), std::ios::cur);
		std::vector<glm::vec4> lighting;
		for (size_t image = 1; image < std::size(Images); ++image)
		{
			const auto& map = Images[image];
			const int channels = map.Format == GL_RGB ? 3 : 2;
			for (int level = 0; level < map.Levels; ++level)
			{
				std::vector<uint16_t> half(map.FaceValues(level) * map.Faces);
				if (!file.read(reinterpret_cast<char*>(half.data()), half.size() * sizeof(uint16_t))) return false;
				for (size_t i = 0; i < half.size(); i += channels)
					lighting.emplace_back(glm::unpackHalf1x16(half[i]), glm::unpackHalf1x16(half[i+1]),
						channels == 3 ? glm::unpackHalf1x16(half[i+2]) : 0.f, 0.f);
			}
		}
		pixels = std::move(lighting);
		return true;
	}
}
