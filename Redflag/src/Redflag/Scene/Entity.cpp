#include "rfpch.h"
#include "Entity.h"
#include"Scene.h"

namespace Redflag
{
	Entity::Entity(entt::entity handle, Scene* scene)
		:m_EntityHandle(handle),m_Scene(scene)
	{

	}
}