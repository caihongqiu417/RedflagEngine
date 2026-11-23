#pragma once

#include "entt.hpp"
#include "Redflag/Animation/Ragdoll.h"
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
	class NpcSystem;

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
		void OnRuntimeStart(uint16_t decisionPort = 8716);
		void OnRuntimeStop();

		/// <returns>Play 期间这局游戏的角色和动作记录。停止播放后返回空指针。</returns>
		NpcSystem* GetNpcs() const { return m_Npcs.get(); }

		void OnSimulationStart();
		void OnSimulationStop();

		// 运行或模拟时推进物理。动态刚体的位置、旋转会写回场景，供渲染和脚本读取。
		void OnUpdatePhysics(Timestep ts);
		/// <param name="movement">控制角色在地面上的 X、Z 方向。零让角色停步，省略则只预览动作。</param>
		void OnUpdateAnimations(Timestep ts, std::optional<glm::vec2> movement = {});

		// 查询最近一次物理步中的碰撞体；距离和半径均以米为单位。
		std::optional<RayHit3D> Raycast(glm::vec3 origin, glm::vec3 direction, float maxDistance, std::optional<uint64_t> ignore = {}) const;
		float SweepSphere(glm::vec3 center, glm::vec3 direction, float radius, float distance, uint64_t ignore) const;
		std::vector<uint64_t> OverlapSphere(glm::vec3 center, float radius) const;
		/// <returns>开始模拟后可读取各肢体的位置。移除组件或停止模拟后，指针失效。</returns>
		const Ragdoll* GetRagdoll(entt::entity handle) const;
		/// <returns>按 Ragdoll::Limb 排列的七个矩阵，把单位立方体放到各肢体的位置并缩放到碰撞盒的大小。</returns>
		std::array<glm::mat4, Ragdoll::Count> GetRagdollMeshes(entt::entity handle) const;

		// 编辑、运行和物理模拟分别更新。
		void OnUpdateEditor(Timestep ts,EditorCamera& camera);
		void OnUpdateRuntime(Timestep ts, glm::vec2 movement = glm::vec2(0));
		void UpdateGame(Timestep ts, glm::vec2 movement = glm::vec2(0));
		bool SetCharacterMovement(UUID id, glm::vec2 direction);
		bool FaceCharacter(UUID id, glm::vec2 direction);
		void OnUpdateSimulation(Timestep ts, EditorCamera& camera, glm::vec2 movement = glm::vec2(0));

		void OnViewportResize(uint32_t width, uint32_t height);

		void DuplicateEntity(Entity entity);

		Entity GetEntityByUUID(UUID uuid);

		entt::registry& GetRegistry() { return m_Registry; }

		Entity GetPrimaryCameraEntity();

		const std::filesystem::path& GetEnvironment() const { return m_Environment; }
		void SetEnvironment(const std::filesystem::path& path) { m_Environment = path; }
		bool IsSkyVisible() const { return m_SkyVisible; }
		void SetSkyVisible(bool visible) { m_SkyVisible = visible; }
	private:
		void OnCameraAdded(CameraComponent& component);
		void ResetCharacterControls();
		void RemoveScript(entt::entity handle);
		void DestroyNativeScript(entt::entity handle);
		void DestroyEmitter(entt::entity handle);

		void OnPhysics2DStart();
		void OnPhysics2DStop();
		void SyncPhysics2D();
		void RemoveBody2D(entt::entity handle);
		template<typename Collider> void RemoveFixture2D(entt::entity handle);

		void SyncPhysics3D();
		void SyncTerrain();
		void RemoveTerrain(entt::entity handle);
		void RemovePhysicsBody(entt::entity handle);
		void RemoveRagdoll(entt::entity handle);
		void StopPhysics3D();
	private:
		entt::registry m_Registry;
		Scope<NpcSystem> m_Npcs;
		std::filesystem::path m_Environment;
		bool m_SkyVisible = true;

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
		std::unordered_map<entt::entity, PhysicsBody3D> m_Bodies3D;
		struct TerrainBody
		{
			uint64_t Entity;
			Ref<Terrain> Data;
			glm::vec3 Position, Rotation, Scale;
			float CellSize, HeightScale;
		};
		std::unordered_map<entt::entity, TerrainBody> m_TerrainBodies;
		std::unordered_map<entt::entity, Scope<Ragdoll>> m_Ragdolls;

		std::unordered_map<UUID, entt::entity> m_EntityMap;

		friend class Entity;
		friend class SceneHierachyPanel;
		friend class SceneSerializer;
		friend class SceneRenderer;  // SceneRenderer 需要访问 m_Registry


	};
}
