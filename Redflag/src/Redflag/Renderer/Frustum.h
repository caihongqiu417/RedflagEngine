#pragma once

/* 用相机的六个边界平面检查包围盒，完全落在任一平面外的网格可以跳过。 */

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

		// OpenGL 中可见点满足 -w ≤ x,y,z ≤ w。把矩阵的第四行与前三行分别相加、相减，就得到六个边界。
		static Frustum FromViewProjection(const glm::mat4& vp)
		{
			// GLM 按列索引，这里从每一列各取一个数拼成一行。
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

			// 把平面的法线长度变成 1，下面的点到平面距离就能用场景的长度单位表示。
			for (auto& p : f.Planes)
			{
				float len = glm::length(glm::vec3(p));
				if (len > 0.0f) p /= len;
			}
			return f;
		}

		// d 是盒子中心到平面的有符号距离，r 是盒子沿法线方向的半宽。
		// 盒子的八个角中，最大的有符号距离是 d + r。连这个角都在平面外时，就可以跳过整个网格。
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
