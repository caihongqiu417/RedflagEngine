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

	// 运行时资源缓存；加载和释放均在持有图形上下文的线程执行。
	class AssetManager
	{
	public:
		// 资源身份
		static AssetLibrary& GetLibrary()
		{
			static AssetLibrary s_Library;
			return s_Library;
		}

		template<typename T>
		static AssetID Add(const std::string& path)
		{
			return GetLibrary().Add(path, AssetTraits<T>::Type);
		}

		// 加载：T 提供 static Ref<T> Load(const std::string&)。
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

			auto asset = T::Load(info->Path.generic_string());
			if (asset)
				assets.emplace(id, asset);
			return asset;
		}

		template<typename T>
		static Ref<T> Load(const std::string& path)
		{
			return Load<T>(Add<T>(path));
		}

		// 缓存只持有共享引用；卸载不会使外部引用失效。
		template<typename T>
		static bool IsLoaded(AssetID id)
		{
			auto* cache = FindCache<T>();
			return cache && cache->Assets.count(id) != 0;
		}

		template<typename T>
		static void Unload(AssetID id)
		{
			if (auto* cache = FindCache<T>())
				cache->Assets.erase(id);
		}

		static void ClearAll() { GetCaches().clear(); }

	private:
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
