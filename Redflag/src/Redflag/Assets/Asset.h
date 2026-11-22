#pragma once

#include <cstdint>
#include <filesystem>

namespace Redflag
{
	using AssetID = uint64_t; // 0 表示未指定资源。

	enum class AssetType { None, Model, Texture };

	/* Path 是源文件的完整路径。模型的编号和类型保存在它旁边的 .rfmeta 文件里。 */
	struct AssetInfo
	{
		AssetID ID = 0;
		AssetType Type = AssetType::None;
		std::filesystem::path Path;
	};

	class Model;
	// 调用 Load<Model> 时，用 AssetType::Model 检查这个文件是否登记为模型。
	template<typename T> struct AssetTraits;
	template<> struct AssetTraits<Model>
	{
		static constexpr AssetType Type = AssetType::Model;
	};
}
