#include "rfpch.h"
#include "TraversalRig.h"
#include "TerrainTraversal.h"
#include "Redflag/Scene/Entity.h"

namespace Redflag
{
	void AdjustTerrainPose(Scene& scene, uint64_t actor, const TransformComponent& transform,
		const TraversalRig& rig, const TerrainTraversal& motion, const ModelAnimation& animation,
		std::vector<ModelAnimation::NodePose>& pose)
	{
		if (!rig.MovementRoot.empty()) animation.PinMovementRoot(pose,rig.MovementRoot);
		if (!motion.HasSupport || motion.Mode == TraversalMode::Jump) return;
		const glm::mat4 model = transform.GetTransform(), fromWorld = glm::inverse(model);
		std::vector<glm::mat4> bones;
		animation.GetModelTransforms(pose, bones);

		for (size_t i=0; i<rig.Limbs.size(); ++i) {
			const auto& limb = rig.Limbs[i];
			glm::mat4 tip;
			if (limb.Tip.empty() || !animation.GetNodeTransform(limb.Tip, bones, tip)) continue;
			const glm::vec3 original(model * tip[3]);
			glm::vec3 target = original;
			if (i < 2) {
				if (motion.Mode == TraversalMode::Climb) continue;
				// 保留动作片段本身的抬脚高度。只补足这只脚与角色中心下方的地面高差。
				const float base = transform.Translation.y - motion.Clearance;
				const auto hit = scene.Raycast({original.x,base+.38f,original.z}, {0,-1,0}, .76f, actor);
				if (!hit || hit->Normal.y < .70f) continue;
				target.y += hit->Position.y - base;
			} else {
				if (motion.Mode != TraversalMode::Climb) continue;
				glm::vec3 across = glm::cross(glm::vec3(0,1,0), motion.Landing - motion.Start);
				across = glm::normalize(across);
				const float spread = glm::clamp(glm::dot(original - transform.Translation, across), -.36f, .36f);
				const float t = motion.Clock / motion.Duration;
				const float contact = glm::smoothstep(0.f,.20f,t) * (1.f-glm::smoothstep(.64f,1.f,t));
				target = glm::mix(original, motion.Ledge + across * spread, contact);
			}
			if (glm::distance(target, original) > .001f)
				animation.SolveLimb(pose, bones, limb, glm::vec3(fromWorld * glm::vec4(target,1)));
		}
	}
}
