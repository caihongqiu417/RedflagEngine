#include "rfpch.h"
#include "Pickup.h"
#include "Scene.h"
#include "Entity.h"

namespace Redflag
{
	bool CanPickUp(Scene& scene, uint64_t candidate, glm::vec3 hand, float reach)
	{
		Entity target = scene.GetEntityByUUID(candidate);
		if (!target || !target.HasComponent<PickupComponent>()) return false;

		glm::vec3 direction = target.GetComponent<TransformComponent>().Translation - hand;
		if (direction == glm::vec3(0)) direction.y = 1;
		std::optional<RayHit3D> hit = scene.Raycast(hand, direction, reach);
		return hit && hit->Entity == candidate;
	}
}
