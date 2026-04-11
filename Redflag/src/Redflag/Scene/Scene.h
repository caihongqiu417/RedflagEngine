#pragma once

#include "entt.hpp"
#include"Redflag/Core/UUID.h"
#include"Components.h"
#include"Redflag/Core/Timestep.h"
#include"Redflag/Renderer/EditorCamera.h"

class b2World;

namespace Redflag {

	class Entity;

	class Scene
	{
	public:
		Scene();
		~Scene();

		static Ref<Scene> Copy(Ref<Scene> other);

		Entity CreateEntity(const std::string& name=std::string());
		Entity CreateEntityWithUUID(UUID uuid,const std::string& name = std::string());
		void DestroyEntity(Entity entity);

		//Start
		void OnRuntimeStart();
		void OnRuntimeStop();

		//OnUpdate;·ÖÎªEditorºÍRuntimae
		void OnUpdateEditor(Timestep ts,EditorCamera& camera);
		void OnUpdateRuntime(Timestep ts);

		void OnViewportResize(uint32_t width, uint32_t height);

		void DuplicateEntity(Entity entity);

		entt::registry& GetRegistry() { return m_Registry; }

		Entity GetPrimaryCameraEntity();
	private:
		template<typename T>
		void OnComponentAdded(Entity entity, T& component);
	private:
		entt::registry m_Registry;

		uint32_t m_ViewportWidth = 0, m_ViewportHeight = 0;

		b2World* m_PhysicsWorld = nullptr;

		friend class Entity;
		friend class SceneHierachyPanel;
		friend class SceneSerializer;
	};
}