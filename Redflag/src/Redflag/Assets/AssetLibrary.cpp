#include "rfpch.h"
#include "AssetLibrary.h"

#include <random>
#include <stdexcept>

namespace Redflag
{
	AssetID AssetLibrary::Add(const std::filesystem::path& path, AssetType type)
	{
		if (path.empty() || !std::filesystem::is_regular_file(path))
			return 0;

		const auto source = std::filesystem::weakly_canonical(std::filesystem::absolute(path));
		const auto meta = MetaPath(source);
		AssetInfo info;
		if (std::filesystem::exists(meta))
		{
			info = Read(meta);
			if (info.Type != type)
				throw std::runtime_error("Asset type mismatch: " + source.string());
		}
		else
		{
			info = { NewID(), type, source };
			std::ofstream out(meta);
			out << "RedflagAsset 1\n" << info.ID << ' ' << static_cast<int>(type) << '\n';
			out.close();
			if (!out)
				throw std::runtime_error("Cannot write asset metadata: " + meta.string());
		}
		Add(info);
		return info.ID;
	}

	void AssetLibrary::Scan(const std::filesystem::path& directory)
	{
		AssetLibrary scanned;
		if (std::filesystem::exists(directory))
			for (const auto& entry : std::filesystem::recursive_directory_iterator(directory))
				if (entry.is_regular_file() && entry.path().extension() == ".rfmeta")
				{
					auto info = Read(entry.path());
					if (std::filesystem::is_regular_file(info.Path))
						scanned.Add(info);
				}
		m_Assets.swap(scanned.m_Assets);
	}

	const AssetInfo* AssetLibrary::Get(AssetID id) const
	{
		auto it = m_Assets.find(id);
		return it == m_Assets.end() ? nullptr : &it->second;
	}

	std::filesystem::path AssetLibrary::MetaPath(const std::filesystem::path& source)
	{
		auto path = source;
		path += ".rfmeta";
		return path;
	}

	AssetInfo AssetLibrary::Read(const std::filesystem::path& path)
	{
		std::ifstream in(path);
		std::string magic;
		int version = 0, type = 0;
		AssetInfo info;
		in >> magic >> version >> info.ID >> type;
		if (!in || magic != "RedflagAsset" || version != 1 || !info.ID ||
			type < static_cast<int>(AssetType::Model) || type > static_cast<int>(AssetType::Texture))
			throw std::runtime_error("Invalid asset metadata: " + path.string());
		info.Type = static_cast<AssetType>(type);
		info.Path = std::filesystem::weakly_canonical(std::filesystem::absolute(path));
		info.Path.replace_extension(); // 仅去掉 .rfmeta，保留源文件扩展名。
		return info;
	}

	AssetID AssetLibrary::NewID() const
	{
		static std::mt19937_64 random(std::random_device{}());
		AssetID id;
		do { id = random(); } while (!id || m_Assets.count(id));
		return id;
	}

	void AssetLibrary::Add(const AssetInfo& info)
	{
		const auto* old = Get(info.ID);
		if (old && std::filesystem::exists(old->Path) && !std::filesystem::equivalent(old->Path, info.Path))
			throw std::runtime_error("Duplicate asset ID: " + info.Path.string());
		m_Assets[info.ID] = info;
	}
}
