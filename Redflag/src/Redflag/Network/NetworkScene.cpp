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
		std::set<uint64_t> incoming;
		for (const WorldPose& pose : poses)
		{
			if (!incoming.insert(pose.ID).second || (!m_RemoteIDs.count(pose.ID) && m_Scene.GetEntityByUUID(pose.ID)))
				return false;
		}

		for (const WorldPose& pose : poses)
		{
			Entity entity = m_Scene.GetEntityByUUID(pose.ID);
			if (!entity)
				entity = m_Scene.CreateEntityWithUUID(pose.ID, "Network Entity");
			TransformComponent& transform = entity.GetComponent<TransformComponent>();
			transform.Translation = pose.Translation;
			transform.Rotation = pose.Rotation;
		}

		for (uint64_t id : m_RemoteIDs)
		{
			if (!incoming.count(id))
			{
				Entity entity = m_Scene.GetEntityByUUID(id);
				if (entity)
					m_Scene.DestroyEntity(entity);
			}
		}
		m_RemoteIDs = std::move(incoming);
		return true;
	}
}
