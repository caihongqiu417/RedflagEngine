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
	 * 运行时资源缓存：按资源类型和稳定 ID 复用 Ref<T>。
	 * 资源库只登记文件，首次 Load 才调用 T::Load 创建对象；缓存与调用方共享所有权。
	 * 涉及 GPU 对象的加载和最后一次释放必须发生在有效图形上下文中。
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
		// T 提供 static Ref<T> Load(const std::string&)；未登记或类型不符返回空引用。
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

			// 只缓存成功结果；返回给组件的 Ref 与缓存指向同一个对象。
			auto asset = T::Load(info->Path.generic_string());
			if (asset)
				assets.emplace(id, asset);
			return asset;
		}

		// 路径入口会先登记元数据，绘制阶段应使用已登记的 ID。
		template<typename T>
		static Ref<T> Load(const std::string& path)
		{
			return Load<T>(Add<T>(path));
		}

		// ---- 查询与卸载 ----
		// 卸载仅移除缓存引用；组件仍持有 Ref 时，对象继续存活。
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

		// 清空对象缓存但保留资源库，关闭图形上下文前调用。
		static void ClearAll() { GetCaches().clear(); }

	private:
		// ---- 类型缓存 ----
		// 外层按 type_index 管理类型，内层按 AssetID 管理对象。
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
