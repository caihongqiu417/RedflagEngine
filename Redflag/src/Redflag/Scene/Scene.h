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
class b2Body;
class b2Fixture;

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

		// 运行或模拟时推进物理。动态刚体的位置、旋转会写回场景，供渲染和脚本读取。
		void OnUpdatePhysics(Timestep ts);
		void OnUpdateAnimations(Timestep ts);

		// 查询最近一次物理步中的碰撞体；距离和半径均以米为单位。
		std::optional<RayHit3D> Raycast(glm::vec3 origin, glm::vec3 direction, float maxDistance) const;
		std::vector<uint64_t> OverlapSphere(glm::vec3 center, float radius) const;

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
		void OnCameraAdded(CameraComponent& component);

		void OnPhysics2DStart();
		void OnPhysics2DStop();
		void SyncPhysics2D();
		void RemoveBody2D(entt::entity handle);
		template<typename Collider> void RemoveFixture2D(entt::entity handle);

		void SyncPhysics3D();
		void RemovePhysicsBody(entt::entity handle);
		void StopPhysics3D();
	private:
		entt::registry m_Registry;
		std::filesystem::path m_Environment;

		uint32_t m_ViewportWidth = 0, m_ViewportHeight = 0;

		struct Body2D
		{
			b2Body* Body = nullptr;
			b2Fixture* Box = nullptr;
			b2Fixture* Circle = nullptr;
		};

		Scope<b2World> m_PhysicsWorld;
		std::unordered_map<entt::entity, Body2D> m_Bodies2D;
		Scope<PhysicsWorld> m_Physics3D;
		// 留下创建碰撞盒时的尺寸和类型，只重建被修改的刚体。
		std::unordered_map<entt::entity, BoxBody3D> m_Boxes3D;

		std::unordered_map<UUID, entt::entity> m_EntityMap;

		friend class Entity;
		friend class SceneHierachyPanel;
		friend class SceneSerializer;
		friend class SceneRenderer;  // SceneRenderer 需要访问 m_Registry


	};
}
