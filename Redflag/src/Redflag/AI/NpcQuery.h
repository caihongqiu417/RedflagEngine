#pragma once

#include "NpcDefinition.h"

namespace Redflag
{
	class Scene;

	struct NpcCandidate
	{
		std::string ID, Intent, Description, Item;
		uint64_t Target;
		float Distance;
		uint64_t Note = 0;
		std::optional<glm::vec3> Place;
	};

	/* 检查场景中的指定队友。Reason 为空时，Candidate 可直接加入模型的候选。 */
	struct NpcTeammateCheck
	{
		NpcCandidate Candidate{};
		const char* Reason = nullptr;
	};

	NpcTeammateCheck CheckTeammateIntent(Scene& scene, uint64_t actor, const NpcDefinition& role, const NpcIntent& intent);

	/* 返回这个角色目前能选择的动作。相同动作的多个目标按距离、实体编号排序。 */
	std::vector<NpcCandidate> FindCandidates(Scene& scene, uint64_t actor, const NpcDefinition& definition, const NpcMemory& memory, bool hasCamera);
}
