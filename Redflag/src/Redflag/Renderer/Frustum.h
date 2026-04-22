#pragma once

// =====================================================================
// Frustum.h — 视锥体 / AABB 剔除工具 (header-only)
//
// 用法：
//   1. Frustum frustum = Frustum::FromViewProjection(camera.VP);
//   2. if (!frustum.IntersectsAABB(worldMin, worldMax)) skip;
//
// 实现：
//   - 从 ViewProjection 矩阵提取 6 个世界空间裁剪平面（Gribb-Hartmann 法）
//   - AABB 测试用 "center/extent + abs(normal)·extent" 公式，
//     每平面 ~10 次乘加，完全避免 8 角点变换，比保守法更快。
// =====================================================================

#include <glm/glm.hpp>
#include <array>

namespace Redflag
{
	class Frustum
	{
	public:
		// 6 个平面：Left / Right / Bottom / Top / Near / Far
		// 每个平面用 vec4(nx, ny, nz, d) 表示，满足 n·p + d = 0
		std::array<glm::vec4, 6> Planes{};

		// -------------------------------------------------------------
		// Gribb-Hartmann：从 ViewProjection 矩阵（行主序视角）直接取平面
		// GLM 矩阵是列主序，m[col][row]，所以取 "行 i" 的表达是 vec4(m[0][i], m[1][i], m[2][i], m[3][i])
		// -------------------------------------------------------------
		static Frustum FromViewProjection(const glm::mat4& vp)
		{
			// 把矩阵转置成"行视角"更直观
			glm::vec4 row0(vp[0][0], vp[1][0], vp[2][0], vp[3][0]);
			glm::vec4 row1(vp[0][1], vp[1][1], vp[2][1], vp[3][1]);
			glm::vec4 row2(vp[0][2], vp[1][2], vp[2][2], vp[3][2]);
			glm::vec4 row3(vp[0][3], vp[1][3], vp[2][3], vp[3][3]);

			Frustum f;
			f.Planes[0] = row3 + row0;  // Left
			f.Planes[1] = row3 - row0;  // Right
			f.Planes[2] = row3 + row1;  // Bottom
			f.Planes[3] = row3 - row1;  // Top
			f.Planes[4] = row3 + row2;  // Near
			f.Planes[5] = row3 - row2;  // Far

			// 归一化（可选；AABB 测试不强制需要，但方便其它用途）
			for (auto& p : f.Planes)
			{
				float len = glm::length(glm::vec3(p));
				if (len > 0.0f) p /= len;
			}
			return f;
		}

		// -------------------------------------------------------------
		// AABB 测试：世界空间包围盒 [min, max] 是否与视锥相交
		//   返回 true  = 至少部分在视锥内（需要渲染）
		//   返回 false = 完全在视锥外（可剔除）
		//
		// 用 center/extent 方法：对每个平面
		//   r = |n·extent|（包围盒在平面法线方向上的投影半径）
		//   d = n·center + plane.w（中心到平面的带符号距离）
		//   若 d + r < 0 → 盒完全在平面负侧 → 完全在外
		// -------------------------------------------------------------
		bool IntersectsAABB(const glm::vec3& minP, const glm::vec3& maxP) const
		{
			glm::vec3 center = 0.5f * (minP + maxP);
			glm::vec3 extent = 0.5f * (maxP - minP);

			for (const glm::vec4& plane : Planes)
			{
				glm::vec3 n = glm::vec3(plane);
				float r = extent.x * glm::abs(n.x) + extent.y * glm::abs(n.y) + extent.z * glm::abs(n.z);
				float d = glm::dot(n, center) + plane.w;
				if (d + r < 0.0f)
					return false;  // 完全在此平面负侧
			}
			return true;
		}
	};
}
