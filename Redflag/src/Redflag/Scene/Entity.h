#pragma once

#include"entt.hpp"
#include"Scene.h"

namespace Redflag {

	class Entity
	{
	public:
		Entity() = default;
		Entity(entt::entity handle,Scene* scene);

		template<typename T,typename...Args>
		T& AddComponent(Args&&... args)
		{
			RF_CORE_ASSERT(!HasComponent<T>(), "Entity already has that component");

			return m_Scene->m_Registry.emplace<T>(m_EntityHandle, std::forward<Args>(args)...);
		}


		template<typename T>
		bool HasComponent()
		{
			return m_Scene->m_Registry.has<T>(m_EntityHandle);
		}

		template<typename T>
		T& GetComponent()
		{
			RF_CORE_ASSERT(HasComponent<T>(), "Entity does not have that component!");
			return m_Scene->m_Registry.get<T>(m_EntityHandle);
		}

		template<typename T>
		T& RemoveComponent()
		{
			RF_CORE_ASSERT(HasComponent<T>(), "Entity does not have that component!");
			return m_Scene->m_Registry.remove<T>(m_EntityHandle);
		}

		operator bool() const { return m_EntityHandle != entt::null; }
	private:
		entt::entity m_EntityHandle{entt::null};
		Scene* m_Scene=nullptr;
	};
}