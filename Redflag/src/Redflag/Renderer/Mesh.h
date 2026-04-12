#pragma once

#include "Redflag/Core/Core.h"
#include "Redflag/Renderer/Buffer.h"
#include "Redflag/Renderer/VertexArray.h"

#include <glm/glm.hpp>
#include <vector>

namespace Redflag
{
	// 单个顶点数据：位置、法线、纹理坐标
	struct MeshVertex
	{
		glm::vec3 Position;
		glm::vec3 Normal;
		glm::vec2 TexCoord;
	};

	class Mesh
	{
	public:
		Mesh(const std::vector<MeshVertex>& vertices, const std::vector<uint32_t>& indices);

		void Bind() const;
		void Unbind() const;

		uint32_t GetVertexCount() const { return m_VertexCount; }
		uint32_t GetIndexCount() const { return m_IndexCount; }
		const Ref<VertexArray>& GetVertexArray() const { return m_VertexArray; }

		// 从顶点/索引数据创建 Mesh
		static Ref<Mesh> Create(const std::vector<MeshVertex>& vertices, const std::vector<uint32_t>& indices);

		// 创建一个硬编码的单位立方体（用于测试）
		static Ref<Mesh> CreateCube();

	private:
		Ref<VertexArray>  m_VertexArray;
		uint32_t          m_VertexCount = 0;
		uint32_t          m_IndexCount = 0;
	};
}
