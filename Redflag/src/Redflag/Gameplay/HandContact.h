#pragma once

#include "Redflag/Animation/ModelAnimation.h"
#include "Redflag/Gameplay/GameplayComponents.h"

namespace Redflag
{
	class Scene;
	class Entity;
	struct TraversalRig;

	/* 随手移动的模型，场景按动态物体绘制。 */
	struct HandProp {};

	/* 交接成功后保存手和目标当时的世界位置，收手时道具随接收方移动。 */
	struct HandContact
	{
		uint64_t Target = 0, Partner = 0, Prop = 0;
		std::string Action;
		glm::vec3 Goal{0}, PropGrip{0}, CommitHand{0}, CommitGoal{0};
		float Weight = 0.f, Age = 0.f, Stow = 0.f, ReachAt = .4f;
		bool Released = false, Passed = false, Timed = false, ShowContact = false;
	};

	bool UsesHandContact(Entity actor, Entity target, const std::string& action);
	bool ApproachContact(Scene& scene, uint64_t actor, uint64_t target, const std::string& action, float seconds);
	bool BeginContact(Scene& scene, uint64_t actor, uint64_t target, const std::string& action);
	bool AtContact(Scene& scene, uint64_t actor);
	void EndContact(Scene& scene, uint64_t actor);
	void CommitContact(Scene& scene, uint64_t actor);
	bool ReadContactSocket(Entity entity, const std::string& socket, glm::vec3& point);
	void PrepareContacts(Scene& scene, float seconds);
	void AdjustHandPose(Entity owner, const TraversalRig& rig, const HandContact& hand, const ModelAnimation& animation,
		std::vector<ModelAnimation::NodePose>& pose);
	void UpdateHandProps(Scene& scene, float seconds);
}
