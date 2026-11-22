#pragma once

#include "DecisionClient.h"
#include <glm/glm.hpp>

namespace Redflag
{
	struct NpcDefinition;
	struct TransformComponent;
	glm::vec3 NpcEyePosition(const TransformComponent& transform, const NpcDefinition& npc);

	/* Rect 从照片左上角量起，依次是左、上、右、下像素。Entity 是拍摄时画在这里的场景物体。 */
	struct PhotoRegion
	{
		uint64_t Entity;
		glm::ivec4 Rect;
		DecisionImage Closeup;
	};

	struct NpcPhoto
	{
		DecisionImage Image;
		uint64_t Observer = 0;
		double Seconds = 0;
		glm::vec3 Eye{}, Forward{};
		glm::ivec2 Size{};
		std::vector<PhotoRegion> Regions;
	};
}
