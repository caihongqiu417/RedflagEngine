#pragma once

#include "Redflag/Animation/ModelAnimation.h"

namespace Redflag
{
	class Scene;
	struct TerrainTraversal;
	struct TransformComponent;

	enum class TraversalLimb { LeftFoot, RightFoot, LeftHand, RightHand };

	/* 不同模型的骨头名字由游戏指定。同一套求解器可以用于人的腿，也可以用于机器人的手臂。 */
	struct TraversalRig
	{
		std::string MovementRoot;
		std::array<ModelAnimation::Limb, 4> Limbs;
	};

	void AdjustTerrainPose(Scene& scene, uint64_t actor, const TransformComponent& transform,
		const TraversalRig& rig, const TerrainTraversal& motion, const ModelAnimation& animation,
		std::vector<ModelAnimation::NodePose>& pose);
}
