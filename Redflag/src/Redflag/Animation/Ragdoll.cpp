#include "rfpch.h"
#include "Ragdoll.h"
#include <PxConfig.h>
#include <PxPhysicsAPI.h>
#include <stdexcept>

namespace Redflag
{
	using namespace physx;

	namespace
	{
		// 七个盒子用米描述。髋、膝、踝的连接点都位于两段肢体的交界处。
		const glm::vec3 Centers[] = {{0,.91f,0}, {-.12f,.665f,0}, {-.12f,.275f,0}, {-.12f,.052f,.065f},
			{.12f,.665f,0}, {.12f,.275f,0}, {.12f,.052f,.065f}};
		const glm::vec3 Sizes[] = {{.18f,.10f,.11f}, {.065f,.195f,.07f}, {.055f,.195f,.055f}, {.085f,.05f,.14f},
			{.065f,.195f,.07f}, {.055f,.195f,.055f}, {.085f,.05f,.14f}};
		const int Parent[] = {-1, 0,1,2, 0,4,5};
		const float PivotsY[] = {0, .86f,.47f,.08f, .86f,.47f,.08f};
		PxVec3 ToPx(glm::vec3 v) { return {v.x,v.y,v.z}; }
	}

	glm::vec3 Ragdoll::HalfSize(Limb limb) { return Sizes[limb]; }

	Ragdoll::Ragdoll(PhysicsWorld& world, const Entities& entities, glm::vec3 origin)
		: m_World(world), m_Entities(entities)
	{
		for (size_t i = 0; i < Count; ++i)
			if (world.GetActor(entities[i]) || std::find(entities.begin(), entities.begin() + i, entities[i]) != entities.begin() + i)
				throw std::invalid_argument("Each limb needs a separate unused entity ID");

		int created = 0;
		try
		{
			for (int i = 0; i < Count; ++i)
			{
				constexpr float density = 950.0f; // kg/m³，用接近水的密度给简化肢体分配质量。
				world.AddBox({entities[i], BodyType3D::Dynamic, origin + Centers[i], {1,0,0,0}, Sizes[i], density});
				++created;
				PxRigidDynamic* body = world.GetActor(entities[i])->is<PxRigidDynamic>();
				m_Mass += body->getMass();
				body->setSolverIterationCounts(12, 4); // 每步多次修正关节的位置与速度，避免脚碰地时膝盖拉开。
				if (i == Pelvis)
					continue;

				glm::vec3 pivot(Centers[i].x, PivotsY[i], 0.0f);
				PxD6Joint* joint = PxD6JointCreate(world.GetScene().getPhysics(), world.GetActor(entities[Parent[i]]),
					PxTransform(ToPx(pivot - Centers[Parent[i]])), body, PxTransform(ToPx(pivot - Centers[i])));
				if (!joint)
					throw std::runtime_error("PxD6JointCreate failed");
				m_Joints[i-1] = joint;
				// 只让关节绕左右方向弯曲，脚和膝盖不会从髋部横向脱开。
				joint->setMotion(PxD6Axis::eTWIST, PxD6Motion::eLIMITED);
				bool knee = i == LeftShin || i == RightShin;
				joint->setTwistLimit(PxJointAngularLimitPair(knee ? -0.08f : -1.15f, knee ? 1.9f : 1.15f));
			}
			// 骨盆在三个方向都可以移动。行走时由角度弹簧辅助维持站姿，放松后弹簧也随之关闭。
			m_Balance = PxD6JointCreate(world.GetScene().getPhysics(), nullptr, PxTransform(PxIdentity),
				world.GetActor(entities[Pelvis]), PxTransform(PxIdentity));
			if (!m_Balance)
				throw std::runtime_error("Could not create pelvis balance joint");
			for (int axis = PxD6Axis::eX; axis <= PxD6Axis::eSWING2; ++axis)
				m_Balance->setMotion(static_cast<PxD6Axis::Enum>(axis), PxD6Motion::eFREE);
		}
		catch (...)
		{
			for (PxD6Joint* joint : m_Joints)
				if (joint) joint->release();
			while (created > 0)
				world.Remove(entities[--created]);
			throw;
		}
	}

	Ragdoll::~Ragdoll()
	{
		m_Balance->release();
		for (PxD6Joint* joint : m_Joints)
			joint->release();
		for (uint64_t entity : m_Entities)
			m_World.Remove(entity);
	}

	void Ragdoll::Relax()
	{
		m_Balance->setDrive(PxD6Drive::eSLERP, PxD6JointDrive());
		for (PxD6Joint* joint : m_Joints)
			joint->setDrive(PxD6Drive::eTWIST, PxD6JointDrive());
	}

	void Ragdoll::DriveWalk(float seconds, float speed)
	{
		// 1.1 米/秒时每秒走完一次左右脚周期。慢走时缩短步幅。
		constexpr float strideSpeed = 1.1f;
		float stride = glm::clamp(speed / strideSpeed, -1.0f, 1.0f);
		m_Phase = std::fmod(m_Phase + seconds * glm::two_pi<float>() * std::min(std::abs(stride), 1.0f), glm::two_pi<float>());
		PxRigidDynamic& pelvis = *m_World.GetActor(m_Entities[Pelvis])->is<PxRigidDynamic>();
		for (int leg = 0; leg < 2; ++leg)
		{
			float phase = m_Phase + leg * glm::pi<float>();
			float hip = -0.4f * stride * std::sin(phase);
			float knee = 0.75f * std::abs(stride) * std::max(0.0f, std::cos(phase));
			float angles[] = {hip, knee, -hip - knee};
			for (int part = 0; part < 3; ++part)
			{
				PxD6Joint& joint = *m_Joints[leg*3 + part];
				// 角度误差产生回正力矩，角速度越大，阻尼越强。这组增益用于上面这副米制骨架。
				joint.setDrive(PxD6Drive::eTWIST, PxD6JointDrive(200.0f, 18.0f, 160.0f));
				joint.setDrivePosition(PxTransform(PxQuat(angles[part], PxVec3(1,0,0))));
			}
		}

		// 地面在脚底附近才辅助保持平衡。腾空时保留重力，角色仍会落下。
		bool supported = false;
		PxRaycastBuffer ground;
		for (Limb foot : {LeftFoot, RightFoot})
		{
			PxRigidActor& actor = *m_World.GetActor(m_Entities[foot]);
			PxVec3 from = actor.getGlobalPose().p + PxVec3(0,.05f,0);
			if (m_World.GetScene().raycast(from, PxVec3(0,-1,0), .15f, ground,
				PxHitFlag::eDEFAULT, PxQueryFilterData(PxQueryFlag::eSTATIC)))
			{
				supported = true;
				break;
			}
		}
		if (!supported)
		{
			m_Balance->setDrive(PxD6Drive::eSLERP, PxD6JointDrive());
			return;
		}
		m_Balance->setDrive(PxD6Drive::eSLERP, PxD6JointDrive(1400.0f, 100.0f, 500.0f));
		PxVec3 velocity = pelvis.getLinearVelocity();
		// 脚还靠近地面时，骨盆低于站立高度就给向上的辅助力。课程里的根部受力控制也允许直接推骨盆。
		float standingHeight = ground.block.position.y + Centers[Pelvis].y;
		float lift = 110.0f * (standingHeight - pelvis.getGlobalPose().p.y) - 18.0f * velocity.y;
		pelvis.addForce(m_Mass * PxVec3(-5.0f * velocity.x, std::max(lift,0.0f), 5.0f * (speed - velocity.z)));
	}
}
