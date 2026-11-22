#pragma once

#include "Asset.h"
#include <unordered_map>

namespace Redflag
{
	/*
	 * 按编号查找模型或贴图的源文件。Add 在源文件旁读写 .rfmeta，Scan 读取目录中的 .rfmeta，重新登记文件。
	 */
	class AssetLibrary
	{
	public:
		// ---- 登记 ----
		// 找不到源文件时返回 0。如果 .rfmeta 损坏、类型不符，或另一份文件用了同一编号，就抛出异常。
		AssetID Add(const std::filesystem::path& path, AssetType type);

		// 替换当前登记表。源文件与 .rfmeta 一起移动时，ID 保持不变。
		void Scan(const std::filesystem::path& directory);

		// ---- 查询与清理 ----
		// 找不到编号时返回 nullptr。结果指向表里的一条记录，重新扫描或清空资源库后要再查一次。
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
