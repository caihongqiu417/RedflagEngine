#pragma once

#include <cstdint>
#include <filesystem>

namespace Redflag
{
	using AssetID = uint64_t; // 0 表示未指定资源。

	enum class AssetType { None, Model, Texture };

	struct AssetInfo
	{
		AssetID ID = 0;
		AssetType Type = AssetType::None;
		std::filesystem::path Path;
	};

	class Model;
	template<typename T> struct AssetTraits;
	template<> struct AssetTraits<Model>
	{
		static constexpr AssetType Type = AssetType::Model;
	};
}
