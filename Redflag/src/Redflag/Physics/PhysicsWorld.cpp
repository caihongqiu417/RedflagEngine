#include "rfpch.h"
#include "PhysicsWorld.h"
#include "Redflag/Terrain/Terrain.h"
#include <PxConfig.h>
#include <PxPhysicsAPI.h>
#include <stdexcept>

namespace Redflag
{
	using namespace physx;

	namespace
	{
		struct Release
		{
			template<class T>
			void operator()(T* object) const { object->release(); }
		};

		template<class T>
		using Owner = std::unique_ptr<T, Release>;

		template<class T>
		Owner<T> Created(T* object, const char* error)
		{
			if (!object)
				throw std::runtime_error(error);
			return Owner<T>(object);
		}

		// PhysX 在同一进程里共用一个 Foundation。最后一个场景关闭后，才释放这组对象。
		struct SDK
		{
			PxDefaultAllocator Allocator;
			PxDefaultErrorCallback Errors;
			Owner<PxFoundation> Foundation = Created(PxCreateFoundation(PX_PHYSICS_VERSION, Allocator, Errors), "PxCreateFoundation");
			Owner<PxPhysics> Physics = Created(PxCreatePhysics(PX_PHYSICS_VERSION, *Foundation, PxTolerancesScale()), "PxCreatePhysics");
		};

		std::shared_ptr<SDK> AcquireSDK()
		{
			static std::weak_ptr<SDK> current;
			std::shared_ptr<SDK> sdk = current.lock();
			if (!sdk)
				current = sdk = std::make_shared<SDK>();
			return sdk;
		}

		PxVec3 ToPx(glm::vec3 v) { return PxVec3(v.x, v.y, v.z); }
		glm::vec3 FromPx(PxVec3 v) { return { v.x, v.y, v.z }; }

		PxTransform Pose(glm::vec3 position, glm::quat q)
		{
			return PxTransform(ToPx(position), PxQuat(q.x, q.y, q.z, q.w));
		}

		struct Body
		{
			uint64_t Entity;
			Owner<PxRigidActor> Actor;
		};

		// 从角色身上发出的射线可以穿过自己的碰撞体，包括同属这个角色的肢体。
		class IgnoreEntity : public PxQueryFilterCallback
		{
		public:
			explicit IgnoreEntity(std::optional<uint64_t> entity) : m_Entity(entity) {}

			PxQueryHitType::Enum preFilter(const PxFilterData&, const PxShape*, const PxRigidActor* actor, PxHitFlags&) override
			{
				return static_cast<const Body*>(actor->userData)->Entity == m_Entity ? PxQueryHitType::eNONE : PxQueryHitType::eBLOCK;
			}

			PxQueryHitType::Enum postFilter(const PxFilterData&, const PxQueryHit&, const PxShape*, const PxRigidActor*) override
			{
				return PxQueryHitType::eBLOCK;
			}

		private:
			std::optional<uint64_t> m_Entity;
		};
	}

	struct PhysicsWorld::World
	{
		std::shared_ptr<SDK> Sdk = AcquireSDK();
		Owner<PxDefaultCpuDispatcher> Dispatcher = Created(PxDefaultCpuDispatcherCreate(1), "PxDefaultCpuDispatcherCreate");
		Owner<PxScene> Scene;
		Owner<PxMaterial> Material;
		std::unordered_map<uint64_t, Body> Bodies;
		double Remainder = 0.0;

		World()
		{
			PxSceneDesc desc(Sdk->Physics->getTolerancesScale());
			desc.gravity = PxVec3(0.0f, -9.81f, 0.0f);
			desc.cpuDispatcher = Dispatcher.get();
			desc.filterShader = PxDefaultSimulationFilterShader;
			Scene = Created(Sdk->Physics->createScene(desc), "PxPhysics::createScene");

			// 普通箱子落地后可以滑动，碰撞时不额外弹起。
			Material = Created(Sdk->Physics->createMaterial(0.5f, 0.5f, 0.0f), "PxPhysics::createMaterial");
		}
	};

	PhysicsWorld::PhysicsWorld() : m_World(std::make_unique<World>()) {}
	PhysicsWorld::~PhysicsWorld() = default;

	PxScene& PhysicsWorld::GetScene() { return *m_World->Scene; }

	PxRigidActor* PhysicsWorld::GetActor(uint64_t entity) const
	{
		auto body = m_World->Bodies.find(entity);
		return body == m_World->Bodies.end() ? nullptr : body->second.Actor.get();
	}

	void PhysicsWorld::AddBox(const BoxBody3D& box, std::optional<uint64_t> owner)
	{
		PxPhysics& physics = *m_World->Sdk->Physics;
		PxTransform pose = Pose(box.Position, box.Rotation);
		PxBoxGeometry shape(ToPx(box.HalfSize));
		PxMaterial& material = *m_World->Material;
		Owner<PxRigidActor> actor;
		if (box.Type == BodyType3D::Static)
			actor = Created(PxCreateStatic(physics, pose, shape, material, PxTransform(ToPx(box.Center))), "PxCreateStatic");
		else
		{
			Owner<PxRigidDynamic> dynamic = Created(PxCreateDynamic(physics, pose, shape, material, box.Density,
				PxTransform(ToPx(box.Center))), "PxCreateDynamic");
			dynamic->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, box.Type == BodyType3D::Kinematic);
			actor = std::move(dynamic);
		}

		Body& body = m_World->Bodies.insert_or_assign(box.Entity, Body{ owner.value_or(box.Entity), std::move(actor) }).first->second;
		body.Actor->userData = &body;
		m_World->Scene->addActor(*body.Actor);
	}

	void PhysicsWorld::AddTerrain(uint64_t entity, const Terrain& data, glm::vec3 position, glm::quat rotation,
		glm::vec3 scale, float cellSize, float heightScale)
	{
		const uint32_t rows = data.GetWidth(), cols = data.GetDepth();
		std::vector<PxHeightFieldSample> samples(size_t(rows) * cols);
		for (uint32_t x = 0; x < rows; ++x)
			for (uint32_t z = 0; z < cols; ++z)
			{
				// PhysX 的行沿 X，列沿 Z。本格两块三角形都经过左上角与右下角。
				PxHeightFieldSample& sample = samples[size_t(x)*cols + z];
				sample.height = static_cast<PxI16>(int(data.GetHeight(x,z)) - 32768);
				sample.materialIndex0 = sample.materialIndex1 = 0;
				sample.setTessFlag();
			}

		PxHeightFieldDesc desc;
		desc.nbRows = rows;
		desc.nbColumns = cols;
		desc.samples.data = samples.data();
		desc.samples.stride = sizeof(PxHeightFieldSample);
		PxPhysics& physics = *m_World->Sdk->Physics;
		Owner<PxHeightField> heights = Created(PxCreateHeightField(desc, physics.getPhysicsInsertionCallback()), "PxCreateHeightField");
		const float unit = heightScale * scale.y / 65535.f;
		PxHeightFieldGeometry shape(heights.get(), PxMeshGeometryFlags(), unit, cellSize*scale.x, cellSize*scale.z);
		// 恢复有符号整数减去的半个量程；图片里的黑色在角色所属实体的 Y=0 处。
		Owner<PxRigidActor> actor = Created(PxCreateStatic(physics, Pose(position, rotation), shape, *m_World->Material,
			PxTransform(PxVec3(0, 32768.f * unit, 0))), "PxCreateStatic(heightfield)");
		Body& body = m_World->Bodies.insert_or_assign(entity, Body{entity, std::move(actor)}).first->second;
		body.Actor->userData = &body;
		m_World->Scene->addActor(*body.Actor);
	}

	void PhysicsWorld::Remove(uint64_t entity)
	{
		m_World->Bodies.erase(entity);
	}

	void PhysicsWorld::Step(float seconds, const std::function<void(float)>& beforeStep)
	{
		// 每次模拟 1/60 秒。高帧率时，把这帧不足一步的时间留给下一帧。
		constexpr double step = 1.0 / 60.0;
		m_World->Remainder += seconds;
		while (m_World->Remainder >= step)
		{
			if (beforeStep)
				beforeStep(static_cast<float>(step));
			m_World->Scene->simulate(static_cast<float>(step));
			m_World->Scene->fetchResults(true);
			m_World->Remainder -= step;
		}
	}

	bool PhysicsWorld::GetPose(uint64_t entity, glm::vec3& position, glm::quat& rotation) const
	{
		PxRigidActor* actor = GetActor(entity);
		if (!actor)
			return false;

		PxTransform pose = actor->getGlobalPose();
		position = FromPx(pose.p);
		rotation = glm::quat(pose.q.w, pose.q.x, pose.q.y, pose.q.z);
		return true;
	}

	void PhysicsWorld::SetKinematicTarget(uint64_t entity, const glm::vec3& position, const glm::quat& rotation)
	{
		PxRigidDynamic* actor = m_World->Bodies.at(entity).Actor->is<PxRigidDynamic>();
		if (actor && actor->getRigidBodyFlags().isSet(PxRigidBodyFlag::eKINEMATIC))
			actor->setKinematicTarget(Pose(position, rotation));
	}

	std::optional<RayHit3D> PhysicsWorld::Raycast(glm::vec3 origin, glm::vec3 direction, float maxDistance, std::optional<uint64_t> ignore) const
	{
		const float length = glm::length(direction);
		PxQueryFilterData filter;
		IgnoreEntity exclude(ignore);
		if (ignore)
			filter.flags |= PxQueryFlag::ePREFILTER;

		PxRaycastBuffer hit;
		if (length <= 0.0f || maxDistance <= 0.0f ||
			!m_World->Scene->raycast(ToPx(origin), ToPx(direction / length), maxDistance, hit, PxHitFlag::eDEFAULT, filter, &exclude))
			return {};

		const PxRaycastHit& face = hit.block;
		return RayHit3D{ static_cast<const Body*>(face.actor->userData)->Entity,
			FromPx(face.position), FromPx(face.normal), face.distance };
	}

	float PhysicsWorld::SweepSphere(glm::vec3 center, glm::vec3 direction, float radius, float distance, uint64_t ignore) const
	{
		const float length = glm::length(direction);
		if (distance <= 0 || length == 0 || radius <= 0) return 0.f;
		PxQueryFilterData filter;
		filter.flags |= PxQueryFlag::ePREFILTER;
		IgnoreEntity skip(ignore);
		PxSweepBuffer hit;
		bool blocked = m_World->Scene->sweep(PxSphereGeometry(radius), PxTransform(ToPx(center)),
			ToPx(direction / length), distance, hit, PxHitFlag::eDEFAULT, filter, &skip);
		return blocked ? hit.block.distance : distance;
	}

	std::vector<uint64_t> PhysicsWorld::OverlapSphere(glm::vec3 center, float radius) const
	{
		std::vector<uint64_t> entities;
		if (radius <= 0.0f || m_World->Bodies.empty())
			return entities;

		// 每个刚体只有一个盒子，给每个盒子留一个位置，查询就能返回范围内的所有实体。
		std::vector<PxOverlapHit> hits(m_World->Bodies.size());
		PxOverlapBuffer result(hits.data(), static_cast<PxU32>(hits.size()));
		m_World->Scene->overlap(PxSphereGeometry(radius), PxTransform(ToPx(center)), result);
		for (PxU32 i = 0; i < result.nbTouches; ++i)
			entities.push_back(static_cast<const Body*>(hits[i].actor->userData)->Entity);
		std::sort(entities.begin(), entities.end());
		entities.erase(std::unique(entities.begin(), entities.end()), entities.end());
		return entities;
	}
}
