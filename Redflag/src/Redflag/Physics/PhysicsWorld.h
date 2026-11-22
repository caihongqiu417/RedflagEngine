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
	enum class BodyType3D { Static, Dynamic, Kinematic };

	struct BoxBody3D
	{
		uint64_t Entity;
		BodyType3D Type;
		glm::vec3 Position;
		glm::quat Rotation;
		glm::vec3 HalfSize; // 从盒子中心到各面的距离，单位为米。
		float Density = 1.0f; // 千克/立方米。质量还取决于盒子的体积。
	};

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
		void Remove(uint64_t entity);
		/// <param name="beforeStep">每次推进物理前调用，传入这一小步的秒数，供角色按当前姿势施力。</param>
		void Step(float seconds, const std::function<void(float)>& beforeStep = {});

		bool GetPose(uint64_t entity, glm::vec3& position, glm::quat& rotation) const;
		void SetKinematicTarget(uint64_t entity, const glm::vec3& position, const glm::quat& rotation);

		/// <summary>从世界坐标 origin 发出射线，返回距离起点最近的碰撞面。</summary>
		/// <param name="maxDistance">射线最远能到达的距离，单位为米。</param>
		std::optional<RayHit3D> Raycast(glm::vec3 origin, glm::vec3 direction, float maxDistance) const;
		std::vector<uint64_t> OverlapSphere(glm::vec3 center, float radius) const;
	private:
		friend class Ragdoll;
		physx::PxScene& GetScene();
		physx::PxRigidActor* GetActor(uint64_t entity) const;

		struct World;
		std::unique_ptr<World> m_World;
	};
}
