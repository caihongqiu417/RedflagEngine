#pragma once

#include <glm/vec3.hpp>
#include <cstdint>
#include <set>
#include <string>

namespace Redflag
{
	/* 区域随实体移动、旋转和缩放。Lights 引用场景中真实的点光源或方向光。 */
	struct EnvironmentZoneComponent
	{
		glm::vec3 HalfSize{1.f}; // 局部坐标中，从中心到各面的距离，单位为米。
		std::string Landform, Vegetation, Landmark;
		std::set<uint64_t> Lights;
	};
}
