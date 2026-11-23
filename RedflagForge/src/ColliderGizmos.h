#pragma once

#include "Redflag/Scene/Entity.h"
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Redflag
{
	/* 两个编辑视口共用同一组世界坐标线段。drawLine 的最后一个参数表示是否选中。 */
	template<class DrawLine>
	void DrawColliderGizmos(Scene& scene, Entity selected, bool showAll, DrawLine drawLine)
	{
		auto boxes = scene.GetRegistry().view<TransformComponent, BoxCollider3DComponent>();
		for (auto id : boxes)
		{
			const bool active = Entity{ id, &scene } == selected;
			if (!active && !showAll) continue;
			const auto& box = boxes.get<BoxCollider3DComponent>(id);
			const glm::mat4 world = boxes.get<TransformComponent>(id).GetTransform();
			glm::vec3 corners[8];
			for (int i = 0; i < 8; ++i)
			{
				const glm::vec3 sign(i & 1 ? 1 : -1, i & 2 ? 1 : -1, i & 4 ? 1 : -1);
				corners[i] = world * glm::vec4(box.Center + sign * box.HalfSize, 1);
			}
			for (int i = 0; i < 8; ++i)
				for (int edge : {1, 2, 4})
					if (!(i & edge)) drawLine(corners[i], corners[i | edge], active);
		}

		auto capsules = scene.GetRegistry().view<TransformComponent, CapsuleCollider3DComponent>();
		for (auto id : capsules)
		{
			const bool active = Entity{ id, &scene } == selected;
			if (!active && !showAll) continue;
			const auto& shape = capsules.get<CapsuleCollider3DComponent>(id);
			const auto& pose = capsules.get<TransformComponent>(id);
			const glm::vec3 center = pose.GetTransform() * glm::vec4(shape.Center, 1);
			const glm::quat rotation(pose.Rotation);
			const glm::vec3 axis = rotation * shape.GetLocalAxis();
			const float radius = shape.GetWorldRadius(pose.Scale);
			const float halfCylinder = .5f * shape.GetWorldHeight(pose.Scale) - radius;
			const glm::vec3 bottom = center - axis * halfCylinder, top = center + axis * halfCylinder;
			glm::vec3 crossAxis(0);
			crossAxis[(int(shape.Axis) + 1) % 3] = 1.f;
			const glm::vec3 side = rotation * crossAxis, front = glm::cross(axis, side);

			// 两个圆是柱身与半球的接缝。缩放后的半径、总长取自物理组件。
			constexpr int segments = 32;
			auto rim = [&](float angle) { return radius * (std::cos(angle) * side + std::sin(angle) * front); };
			for (int i = 0; i < segments; ++i)
			{
				const glm::vec3 a = rim(glm::two_pi<float>() * i / segments);
				const glm::vec3 b = rim(glm::two_pi<float>() * (i + 1) / segments);
				drawLine(bottom + a, bottom + b, active);
				drawLine(top + a, top + b, active);
			}
			for (const glm::vec3 radial : {side, -side, front, -front})
			{
				drawLine(bottom + radius * radial, top + radius * radial, active);
				glm::vec3 lastUpper = top + radius * radial, lastLower = bottom + radius * radial;
				for (int i = 1; i <= segments / 4; ++i)
				{
					const float angle = glm::two_pi<float>() * i / segments;
					const glm::vec3 ring = radius * std::cos(angle) * radial;
					const glm::vec3 rise = radius * std::sin(angle) * axis;
					const glm::vec3 upper = top + ring + rise, lower = bottom + ring - rise;
					drawLine(lastUpper, upper, active);
					drawLine(lastLower, lower, active);
					lastUpper = upper;
					lastLower = lower;
				}
			}
		}
	}
}
