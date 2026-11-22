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
	};

	/* 返回这个角色目前能选择的动作。相同动作的多个目标按距离、实体编号排序。 */
	std::vector<NpcCandidate> FindCandidates(Scene& scene, uint64_t actor, const NpcDefinition& definition);
}
