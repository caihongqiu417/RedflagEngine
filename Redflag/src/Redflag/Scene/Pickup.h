#pragma once
#include <cstdint>
#include <glm/vec3.hpp>

namespace Redflag
{
	class Scene;

	// 收到图片中的物体编号后，在当前场景从手的位置做射线检测。reach 为最远拿取距离（米）。
	bool CanPickUp(Scene& scene, uint64_t candidate, glm::vec3 hand, float reach);
}
