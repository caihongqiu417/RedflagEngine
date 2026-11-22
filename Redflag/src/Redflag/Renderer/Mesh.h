#pragma once

#include "Redflag/Core/Core.h"
#include "Redflag/Renderer/Buffer.h"
#include "Redflag/Renderer/VertexArray.h"

#include <glm/glm.hpp>
#include <vector>

namespace Redflag
{
	/* 后端持有上传好的顶点和索引，同一份几何可以画在多个位置。 */
	struct MeshGeometry { virtual ~MeshGeometry() = default; };

	struct MeshVertex
	{
		glm::vec3 Position;
		glm::vec3 Normal;
		glm::vec2 TexCoord;
		glm::vec3 Tangent;   // 切线向量，用于法线贴图的 TBN 矩阵构建
		glm::vec2 EmissiveUVOffset{0}; // 加到 TexCoord 上即为发光图坐标。共用同一套 UV 时为零。
	};

	/* 网格持有当前后端创建的几何数据。包围盒保留在模型坐标中，供场景判断可见性。 */
	class Mesh
	{
	public:
		// ---- 创建与绑定 ----
		Mesh(const std::vector<MeshVertex>& vertices, const std::vector<uint32_t>& indices);

		void Bind() const;
		void Unbind() const;

		// ---- 几何查询 ----
		uint32_t GetVertexCount() const { return m_VertexCount; }
		uint32_t GetIndexCount() const { return m_IndexCount; }
		const Ref<MeshGeometry>& GetGeometry() const { return m_Geometry; }
		const Ref<VertexArray>& GetVertexArray() const; // 原 OpenGL 绘制使用的顶点数组。

		// 本地空间轴对齐包围盒（构造时从顶点扫出，用于视锥体剪裁）
		const glm::vec3& GetAABBMin() const { return m_AABBMin; }
		const glm::vec3& GetAABBMax() const { return m_AABBMax; }

		// 从顶点/索引数据创建 Mesh
		static Ref<Mesh> Create(const std::vector<MeshVertex>& vertices, const std::vector<uint32_t>& indices);

		// 创建一个硬编码的单位立方体（用于测试）
		static Ref<Mesh> CreateCube();

	private:
		Ref<MeshGeometry> m_Geometry;
		uint32_t          m_VertexCount = 0;
		uint32_t          m_IndexCount = 0;

		glm::vec3 m_AABBMin{ 0.0f };
		glm::vec3 m_AABBMax{ 0.0f };
	};
}
