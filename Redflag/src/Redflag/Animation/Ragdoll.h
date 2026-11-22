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

		/// <param name="origin">站立时两脚之间的地面位置，单位为米；角色朝模型的 +Z。</param>
		Ragdoll(PhysicsWorld& world, const Entities& entities, glm::vec3 origin);
		~Ragdoll();
		Ragdoll(const Ragdoll&) = delete;
		Ragdoll& operator=(const Ragdoll&) = delete;

		// 在每次固定物理步之前调用。速度沿 +Z，单位为米/秒。
		void DriveWalk(float seconds, float speed);
		void Relax();
		const Entities& GetEntities() const { return m_Entities; }
		static glm::vec3 HalfSize(Limb limb);

	private:
		PhysicsWorld& m_World;
		Entities m_Entities;
		std::array<physx::PxD6Joint*, Count - 1> m_Joints{};
		physx::PxD6Joint* m_Balance = nullptr;
		float m_Phase = 0.0f, m_Mass = 0.0f;
	};
}
