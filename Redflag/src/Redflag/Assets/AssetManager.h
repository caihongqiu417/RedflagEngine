#pragma once

#include <memory>
#include <utility>
#include "Redflag/Core/Core.h"

#include "AssetLibrary.h"
#include <string>
#include <typeindex>
#include <unordered_map>

namespace Redflag
{

	/*
	 * 同一个模型被多个实体使用时，只加载一次。
	 * Add 记下文件的位置和 ID。Load 第一次读文件，以后返回已经加载的对象。
	 * 模型的顶点和贴图要交给显卡，创建和释放都要在图形上下文关闭前完成。
	 */
	class AssetManager
	{
	public:
		// ---- 资源登记 ----
		static AssetLibrary& GetLibrary()
		{
			static AssetLibrary s_Library;
			return s_Library;
		}

		// 只登记 ID，不导入模型或创建 GPU 对象。
		template<typename T>
		static AssetID Add(const std::string& path)
		{
			return GetLibrary().Add(path, AssetTraits<T>::Type);
		}

		// ---- 加载 ----
		// 首次加载模型会调用 Model::Load 读文件。找不到这个 ID，或它登记的是另一类资源时，返回 nullptr。
		template<typename T>
		static Ref<T> Load(AssetID id)
		{
			const auto* info = GetLibrary().Get(id);
			if (!info || info->Type != AssetTraits<T>::Type)
				return nullptr;
			auto& assets = GetCache<T>().Assets;
			auto it = assets.find(id);
			if (it != assets.end())
				return it->second;

			// 加载失败时不记进缓存。修好文件后，下次 Load 还会再试。
			auto asset = T::Load(info->Path.generic_string());
			if (asset)
				assets.emplace(id, asset);
			return asset;
		}

		// 按文件名加载时先登记资源。场景绘制同一模型时可以直接使用它的 ID。
		template<typename T>
		static Ref<T> Load(const std::string& path)
		{
			return Load<T>(Add<T>(path));
		}

		// ---- 查询与卸载 ----
		template<typename T>
		static bool IsLoaded(AssetID id)
		{
			auto* cache = FindCache<T>();
			return cache && cache->Assets.count(id) != 0;
		}

		// 移出缓存后，场景里还在用这个模型的实体可以继续画它。
		template<typename T>
		static void Unload(AssetID id)
		{
			if (auto* cache = FindCache<T>())
				cache->Assets.erase(id);
		}

		// 关闭图形设备前调用，清空管理器保存的模型和贴图。文件路径和 ID 仍在资源库里。
		static void ClearAll() { GetCaches().clear(); }

	private:
		// ---- 类型缓存 ----
		// 每种资源类型各有一张表。加载模型时，按 ID 去模型表里找。
		struct CacheBase
		{
			virtual ~CacheBase() = default;
		};

		template<typename T>
		struct Cache : CacheBase
		{
			std::unordered_map<AssetID, Ref<T>> Assets;
		};

		static std::unordered_map<std::type_index, Scope<CacheBase>>& GetCaches()
		{
			static std::unordered_map<std::type_index, Scope<CacheBase>> s_Caches;
			return s_Caches;
		}

		template<typename T>
		static Cache<T>* FindCache()
		{
			auto& caches = GetCaches();
			auto it = caches.find(typeid(T));
			return it == caches.end() ? nullptr : static_cast<Cache<T>*>(it->second.get());
		}

		template<typename T>
		static Cache<T>& GetCache()
		{
			auto& cache = GetCaches()[typeid(T)];
			if (!cache)
				cache = CreateScope<Cache<T>>();
			return *static_cast<Cache<T>*>(cache.get());
		}
	};

}
