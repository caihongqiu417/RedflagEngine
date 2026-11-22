#pragma once

#include "entt.hpp"
#include <filesystem>
#include"Redflag/Core/UUID.h"
#include"Components.h"
#include"Redflag/Core/Timestep.h"
#include"Redflag/Renderer/EditorCamera.h"
#include"Redflag/Renderer/SceneRenderer.h"
#include"Redflag/Scripting/ScriptEngine.h"

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

		void OnSimulationStart();
		void OnSimulationStop();

		// 编辑、运行和物理模拟分别更新。
		void OnUpdateEditor(Timestep ts,EditorCamera& camera);
		void OnUpdateRuntime(Timestep ts);
		void OnUpdateSimulation(Timestep ts, EditorCamera& camera);

		void OnViewportResize(uint32_t width, uint32_t height);

		void DuplicateEntity(Entity entity);

		Entity GetEntityByUUID(UUID uuid);

		entt::registry& GetRegistry() { return m_Registry; }

		Entity GetPrimaryCameraEntity();

		const std::filesystem::path& GetEnvironment() const { return m_Environment; }
		void SetEnvironment(const std::filesystem::path& path) { m_Environment = path; }
	private:
		template<typename T>
		void OnComponentAdded(Entity entity, T& component);

		void OnPhysics2DStart();
		void OnPhysics2DStop();
	private:
		entt::registry m_Registry;
		std::filesystem::path m_Environment;

		uint32_t m_ViewportWidth = 0, m_ViewportHeight = 0;

		b2World* m_PhysicsWorld = nullptr;

		std::unordered_map<UUID, entt::entity> m_EntityMap;

		friend class Entity;
		friend class SceneHierachyPanel;
		friend class SceneSerializer;
		friend class SceneRenderer;  // SceneRenderer 需要访问 m_Registry


	};
}
