#pragma once

#include "Redflag/Core/Core.h"
#include <unordered_map>
#include <string>

namespace Redflag {

	// -----------------------------------------------------------------------
	// AssetManager
	//
	// 简单的路径缓存：同路径只加载一次，返回共享的 Ref<T>。
	// 约定：每个可被管理的资源类型 T 必须提供：
	//   static Ref<T> Load(const std::string& path);
	//
	// 用法：
	//   auto model = AssetManager::GetOrLoad<Model>("assets/models/gun.obj");
	// -----------------------------------------------------------------------

	class AssetManager
	{
	public:
		// 主查询接口：命中缓存直接返回，否则调用 T::Load(path) 并缓存
		template<typename T>
		static Ref<T> GetOrLoad(const std::string& path)
		{
			auto& cache = GetCache<T>();
			auto it = cache.find(path);
			if (it != cache.end())
				return it->second;

			Ref<T> asset = T::Load(path);
			if (asset)
				cache[path] = asset;
			return asset;
		}

		// 检查路径是否已缓存
		template<typename T>
		static bool IsLoaded(const std::string& path)
		{
			return GetCache<T>().count(path) > 0;
		}

		// 从缓存移除，引用归零后资源自动释放
		template<typename T>
		static void Unload(const std::string& path)
		{
			GetCache<T>().erase(path);
		}

		// 清理指定类型的所有缓存
		template<typename T>
		static void Clear()
		{
			GetCache<T>().clear();
		}

	private:
		// 每个类型 T 持有独立的静态 map
		template<typename T>
		static std::unordered_map<std::string, Ref<T>>& GetCache()
		{
			static std::unordered_map<std::string, Ref<T>> s_Cache;
			return s_Cache;
		}
	};

}
