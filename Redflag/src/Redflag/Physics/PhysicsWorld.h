#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>
#include <glm/vec3.hpp>
#include <glm/gtc/quaternion.hpp>

namespace physx { class PxScene; class PxRigidActor; }

namespace Redflag
{
	class Terrain;
	enum class BodyType3D { Static, Dynamic, Kinematic };
	enum class ColliderShape3D { Box, Capsule };
	enum class CapsuleAxis { X, Y, Z };

	struct PhysicsBody3D
	{
		uint64_t Entity;
		BodyType3D Type;
		glm::vec3 Position;
		glm::quat Rotation;
		glm::vec3 HalfSize; // 半尺寸，以米计。胶囊取 {半径，总高的一半，半径}。
		float Density = 1.0f; // 千克/立方米。质量还取决于盒子的体积。
		glm::vec3 Center{0}; // 碰撞体中心相对于刚体原点的位置，单位为米。
		ColliderShape3D Shape = ColliderShape3D::Box;
		CapsuleAxis Axis = CapsuleAxis::Y;
	};

	using BoxBody3D = PhysicsBody3D; // 保留原有 AddBox 调用所用的类型。

	struct RayHit3D
	{
		uint64_t Entity;
		glm::vec3 Position, Normal;
		float Distance;
	};

	/* 一个运行中的场景持有一个物理世界。刚体通过实体编号和场景中的物体对应。 */
	class PhysicsWorld
	{
	public:
		PhysicsWorld();
		~PhysicsWorld();

		/// <param name="owner">几个碰撞盒属于同一角色时，射线和范围查询都返回角色的实体编号。</param>
		void AddBox(const BoxBody3D& box, std::optional<uint64_t> owner = {});
		void AddBody(const PhysicsBody3D& body, std::optional<uint64_t> owner = {});
		void AddTerrain(uint64_t entity, const Terrain& data, glm::vec3 position, glm::quat rotation,
			glm::vec3 scale, float cellSize, float heightScale);
		void Remove(uint64_t entity);
		/// <param name="beforeStep">每次推进物理前调用，传入这一小步的秒数，供角色按当前姿势施力。</param>
		void Step(float seconds, const std::function<void(float)>& beforeStep = {});

		bool GetPose(uint64_t entity, glm::vec3& position, glm::quat& rotation) const;
		void SetKinematicTarget(uint64_t entity, const glm::vec3& position, const glm::quat& rotation);

		/// <summary>从世界坐标 origin 发出射线，返回距离起点最近的碰撞面。</summary>
		/// <param name="maxDistance">射线最远能到达的距离，单位为米。</param>
		std::optional<RayHit3D> Raycast(glm::vec3 origin, glm::vec3 direction, float maxDistance,
			std::optional<uint64_t> ignore = {}) const;
		std::vector<uint64_t> OverlapSphere(glm::vec3 center, float radius) const;

		/// <summary>沿指定方向推动一个球，返回它的中心在碰到物体前能移动多少米。</summary>
		float SweepSphere(glm::vec3 center, glm::vec3 direction, float radius, float distance, uint64_t ignore,
			bool allowSeparation = false) const;
	private:
		friend class Ragdoll;
		physx::PxScene& GetScene();
		physx::PxRigidActor* GetActor(uint64_t entity) const;

		struct World;
		std::unique_ptr<World> m_World;
	};
}
