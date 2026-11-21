#pragma once

#include <cstdint>
#include <filesystem>

namespace Redflag
{
	using AssetID = uint64_t; // 0 表示未指定资源。

	enum class AssetType { None, Model, Texture };

	/* 资源身份和源文件位置。Path 在登记时转为规范绝对路径，ID 写入旁置 .rfmeta。 */
	struct AssetInfo
	{
		AssetID ID = 0;
		AssetType Type = AssetType::None;
		std::filesystem::path Path;
	};

	class Model;
	// 支持进入 AssetManager 的类型在这里声明对应的元数据类型。
	template<typename T> struct AssetTraits;
	template<> struct AssetTraits<Model>
	{
		static constexpr AssetType Type = AssetType::Model;
	};
}
