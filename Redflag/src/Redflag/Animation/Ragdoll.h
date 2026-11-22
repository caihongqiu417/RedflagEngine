#pragma once

#include "Redflag/Physics/PhysicsWorld.h"
#include <array>

namespace physx { class PxD6Joint; }

namespace Redflag
{
	/* 七段下半身共用场景的物理世界。先销毁这个角色，再销毁 world。 */
	class Ragdoll
	{
	public:
		enum Limb { Pelvis, LeftThigh, LeftShin, LeftFoot, RightThigh, RightShin, RightFoot, Count };
		using Entities = std::array<uint64_t, Count>;

		/// <param name="origin">初始姿势下两脚之间的位置，单位为米。</param>
		Ragdoll(PhysicsWorld& world, const Entities& entities, glm::vec3 origin,
			glm::quat rotation = {1,0,0,0}, std::optional<uint64_t> owner = {});
		~Ragdoll();
		Ragdoll(const Ragdoll&) = delete;
		Ragdoll& operator=(const Ragdoll&) = delete;

		// 在每次固定物理步之前调用。正速度沿角色开始时的朝向，单位为米/秒。
		void DriveWalk(float seconds, float speed);
		void Relax();
		const Entities& GetEntities() const { return m_Entities; }
		void GetPose(Limb limb, glm::vec3& position, glm::quat& rotation) const;
		static glm::vec3 Center(Limb limb);
		static glm::vec3 HalfSize(Limb limb);

	private:
		PhysicsWorld& m_World;
		Entities m_Entities;
		glm::vec3 m_Forward;
		std::array<physx::PxD6Joint*, Count - 1> m_Joints{};
		physx::PxD6Joint* m_Balance = nullptr;
		bool m_Driven = false;
		float m_Phase = 0.0f, m_Mass = 0.0f;
	};
}
