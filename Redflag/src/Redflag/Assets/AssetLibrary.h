#pragma once

#include "Asset.h"
#include <unordered_map>

namespace Redflag
{
	// 只管理资源身份与位置，不加载模型，也不创建 GPU 资源。
	class AssetLibrary
	{
	public:
		AssetID Add(const std::filesystem::path& path, AssetType type);

		// 扫描已有元数据；源文件与 .rfmeta 一起移动时，ID 保持不变。
		void Scan(const std::filesystem::path& directory);

		const AssetInfo* Get(AssetID id) const;

		void Clear() { m_Assets.clear(); }

	private:
		static std::filesystem::path MetaPath(const std::filesystem::path& source);

		static AssetInfo Read(const std::filesystem::path& path);

		AssetID NewID() const;

		void Add(const AssetInfo& info);

		std::unordered_map<AssetID, AssetInfo> m_Assets;
	};
}
