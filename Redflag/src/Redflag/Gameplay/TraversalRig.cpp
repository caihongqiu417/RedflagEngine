#include "rfpch.h"
#include "TraversalRig.h"
#include "TerrainTraversal.h"
#include "Redflag/Scene/Entity.h"

namespace Redflag
{
	namespace
	{
		float FootAdjustment(Scene& scene, uint64_t actor, float base, const glm::mat4& model,
			const ModelAnimation& animation, const std::vector<glm::mat4>& bones,
			const std::string& ankle, const std::vector<TraversalRig::FootContact>& sole)
		{
			float lowest = std::numeric_limits<float>::max(), standing = lowest, support = -lowest;
			auto measure = [&](const std::string& bone, const glm::vec3& point) {
				glm::mat4 pose, rest;
				if (!animation.GetNodeTransform(bone,bones,pose) || !animation.GetRestTransform(bone,rest)) return;
				const glm::vec3 p(model * pose * glm::vec4(point,1));
				lowest = std::min(lowest,p.y);
				standing = std::min(standing,(model * rest * glm::vec4(point,1)).y);

				const auto hit = scene.Raycast({p.x,base+.38f,p.z}, {0,-1,0}, .76f, actor);
				// 从箱子里面发出的射线会命中起点。这里没有踏面，不要把鞋推高。
				const float height = hit && hit->Distance > 0.f && hit->Normal.y >= .70f ? hit->Position.y : base;
				support = std::max(support,height-p.y);
			};
			if (sole.empty()) measure(ankle,glm::vec3(0));
			else for (const auto& contact : sole) measure(contact.Bone,contact.Point);
			if (lowest == std::numeric_limits<float>::max()) return 0.f;

			// 以鞋底最先接触的那块地面为准。翘起鞋跟而前掌仍着地时，前掌仍会踩上台阶。
			const float groundDelta = support+lowest-base;
			if (!sole.empty() && groundDelta > .001f)
				return std::max(0.f,support); // 只补鞋底真正穿入台面的深度，已经越过台面的脚保持原姿势。
			const float lift = std::max(0.f,lowest-standing);
			return std::copysign(std::max(0.f,std::abs(groundDelta)-lift),groundDelta);
		}
	}

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
				const float base = transform.Translation.y - motion.Clearance;
				target.y += FootAdjustment(scene,actor,base,model,animation,bones,limb.Tip,rig.Soles[i]);
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
				animation.SolveLimb(pose, bones, limb, glm::vec3(fromWorld * glm::vec4(target,1)), i < 2 /* 保留脚掌朝向 */);
		}
	}
}
