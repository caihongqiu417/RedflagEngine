#pragma once

#include <filesystem>
#include <vector>
#include <glm/vec4.hpp>

namespace Redflag
{
	struct IBLTextures;

	/* 一份环境光烘焙文件包含天空、漫反射、粗糙度分层的倒影和 BRDF 查找表。 */
	namespace IBLArchive
	{
		bool Save(const std::filesystem::path& path, const IBLTextures& textures);

		// 成功时返回四张新纹理，调用者接管它们。文件无效时保留调用者原来的结果。
		bool Load(const std::filesystem::path& path, IBLTextures& textures);

		// 计算着色只用漫反射图、五层镜面反射和 BRDF 表。每个纹素展开为一个 vec4。
		bool ReadLighting(const std::filesystem::path& path, std::vector<glm::vec4>& pixels);
	}
}
