#pragma once
#include "Redflag/Scene/Entity.h"
#include <optional>

namespace Redflag
{
	/* Center 是世界位置。HalfSize 以米计，可按指定朝向量出角色或设备占用的宽、高、深。 */
	struct ColliderBounds
	{
		glm::vec3 Center, HalfSize;
	};

	inline std::optional<ColliderBounds> GetColliderBounds(Entity entity, glm::quat axes = glm::quat(1,0,0,0))
	{
		const TransformComponent& tr = entity.GetComponent<TransformComponent>();
		const glm::quat orientation(tr.Rotation);
		const glm::mat3 relative = glm::mat3_cast(glm::conjugate(axes) * orientation);
		glm::vec3 center, halfSize;

		if (entity.HasComponent<CapsuleCollider3DComponent>()) {
			const auto& capsule = entity.GetComponent<CapsuleCollider3DComponent>();
			const float radius = capsule.GetWorldRadius(tr.Scale);
			const float halfSegment = capsule.GetWorldHeight(tr.Scale) * .5f - radius;
			// 先量胶囊中间的直段，再加上两端圆帽。倒下和站立可以共用这一计算。
			halfSize = glm::abs(relative * capsule.GetLocalAxis()) * halfSegment + glm::vec3(radius);
			center = capsule.Center;
		} else if (entity.HasComponent<BoxCollider3DComponent>()) {
			const auto& box = entity.GetComponent<BoxCollider3DComponent>();
			const glm::vec3 size = box.HalfSize * glm::abs(tr.Scale);
			halfSize = glm::abs(relative[0]) * size.x + glm::abs(relative[1]) * size.y + glm::abs(relative[2]) * size.z;
			center = box.Center;
		} else return {};

		return ColliderBounds{tr.Translation + orientation * (center * tr.Scale), halfSize};
	}

	inline float CharacterRadius(Entity owner)
	{
		const glm::vec3 scale = owner.GetComponent<TransformComponent>().Scale;
		if (owner.HasComponent<CapsuleCollider3DComponent>())
			return owner.GetComponent<CapsuleCollider3DComponent>().GetWorldRadius(scale);
		if (owner.HasComponent<BoxCollider3DComponent>())
			return owner.GetComponent<BoxCollider3DComponent>().HalfSize.x * std::abs(scale.x);
		return .30f; // 旧场景里只带模型的角色沿用原有的移动半径，单位为米。
	}
}
