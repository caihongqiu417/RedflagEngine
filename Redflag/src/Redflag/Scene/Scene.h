#pragma once

#include "entt.hpp"
#include"Components.h"
#include"Redflag/Core/Timestep.h"
#include"Redflag/Renderer/EditorCamera.h"


namespace Redflag {

	class Entity;

	class Scene
	{
	public:
		Scene();
		~Scene();

		Entity CreateEntity(const std::string& name=std::string());
		void DestroyEntity(Entity entity);

		//OnUpdate;·ÖÎªEditorºÍRuntimae
		void OnUpdateEditor(Timestep ts,EditorCamera& camera);
		void OnUpdateRuntime(Timestep ts);

		void OnViewportResize(uint32_t width, uint32_t height);

		entt::registry& GetRegistry() { return m_Registry; }

		Entity GetPrimaryCameraEntity();
	private:
		template<typename T>
		void OnComponentAdded(Entity entity, T& component);
	private:
		entt::registry m_Registry;
		uint32_t m_ViewportWidth = 0, m_ViewportHeight = 0;

		friend class Entity;
		friend class SceneHierachyPanel;
		friend class SceneSerializer;
	};
}