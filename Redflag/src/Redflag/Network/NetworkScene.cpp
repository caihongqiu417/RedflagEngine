#include "rfpch.h"
#include "NetworkScene.h"
#include "Redflag/Scene/Scene.h"
#include "Redflag/Scene/Entity.h"

namespace Redflag
{
	std::vector<WorldPose> CapturePoses(Scene& scene, const std::vector<uint64_t>& ids)
	{
		std::vector<WorldPose> poses;
		for (uint64_t id : ids)
		{
			Entity entity = scene.GetEntityByUUID(id);
			if (!entity)
				continue;
			const TransformComponent& transform = entity.GetComponent<TransformComponent>();
			poses.push_back({ id, transform.Translation, transform.Rotation });
		}
		return poses;
	}

	NetworkScene::~NetworkScene()
	{
		Apply({});
	}

	bool NetworkScene::Apply(const std::vector<WorldPose>& poses)
	{
		std::unordered_map<uint64_t, entt::entity> incoming;
		for (const WorldPose& pose : poses)
		{
			Entity entity = m_Scene.GetEntityByUUID(pose.ID);
			const auto replica = m_Replicas.find(pose.ID);
			if (entity && (replica == m_Replicas.end() || replica->second != static_cast<entt::entity>(entity)))
				return false;
			if (!incoming.emplace(pose.ID, static_cast<entt::entity>(entity)).second)
				return false;
		}

		for (const WorldPose& pose : poses)
		{
			Entity entity = m_Scene.GetEntityByUUID(pose.ID);
			if (!entity)
			{
				entity = m_Scene.CreateEntityWithUUID(pose.ID, "Network Entity");
				incoming.at(pose.ID) = entity;
			}
			TransformComponent& transform = entity.GetComponent<TransformComponent>();
			transform.Translation = pose.Translation;
			transform.Rotation = pose.Rotation;
		}

		for (const auto& [id, handle] : m_Replicas)
		{
			// EnTT 句柄会随对象换代。旧副本被游戏删除后，断线也不能删掉后来恢复的物体。
			if (!incoming.count(id) && m_Scene.GetRegistry().valid(handle))
				m_Scene.DestroyEntity(Entity(handle, &m_Scene));
		}
		m_Replicas = std::move(incoming);
		return true;
	}
}
