#pragma once

#include "Asset.h"
#include <unordered_map>

namespace Redflag
{
	/*
	 * 资源库：保存 ID 到类型、源文件路径的映射，不持有模型或 GPU 资源。
	 * Add 读取或创建源文件旁的 .rfmeta，Scan 用目录中的元数据重建整张表。
	 */
	class AssetLibrary
	{
	public:
		// ---- 登记 ----
		// 源文件不存在时返回 0，元数据损坏或身份冲突时抛出异常。
		AssetID Add(const std::filesystem::path& path, AssetType type);

		// 替换当前登记表。源文件与 .rfmeta 一起移动时，ID 保持不变。
		void Scan(const std::filesystem::path& directory);

		// ---- 查询与清理 ----
		// 未登记返回 nullptr；返回指针归资源库所有，Scan/Clear 后不可继续使用。
		const AssetInfo* Get(AssetID id) const;

		void Clear() { m_Assets.clear(); }

	private:
		// ---- 元数据 ----
		static std::filesystem::path MetaPath(const std::filesystem::path& source);

		static AssetInfo Read(const std::filesystem::path& path);

		AssetID NewID() const;

		void Add(const AssetInfo& info);

		// ---- 资源表 ----
		std::unordered_map<AssetID, AssetInfo> m_Assets;
	};
}
