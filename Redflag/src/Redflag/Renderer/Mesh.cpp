#include "rfpch.h"
#include "Mesh.h"
#include "Platform/OpenGL/OpenGLMeshGeometry.h"
#include "Redflag/Renderer/RHI/RenderDevice.h"

namespace Redflag
{
	Mesh::Mesh(const std::vector<MeshVertex>& vertices, const std::vector<uint32_t>& indices)
	{
		m_VertexCount = (uint32_t)vertices.size();
		m_IndexCount = (uint32_t)indices.size();

		// 计算本地空间 AABB（用于视锥体剪裁）
		if (!vertices.empty())
		{
			m_AABBMin = m_AABBMax = vertices[0].Position;
			for (size_t i = 1; i < vertices.size(); ++i)
			{
				const glm::vec3& p = vertices[i].Position;
				m_AABBMin = glm::min(m_AABBMin, p);
				m_AABBMax = glm::max(m_AABBMax, p);
			}
		}

		m_Geometry = RenderDevice::Get().CreateMesh(vertices, indices);
	}

	const Ref<VertexArray>& Mesh::GetVertexArray() const
	{
		return dynamic_cast<const OpenGLMeshGeometry&>(*m_Geometry).Array;
	}

	void Mesh::Bind() const
	{
		GetVertexArray()->Bind();
	}

	void Mesh::Unbind() const
	{
		GetVertexArray()->Unbind();
	}

	Ref<Mesh> Mesh::Create(const std::vector<MeshVertex>& vertices, const std::vector<uint32_t>& indices)
	{
		return CreateRef<Mesh>(vertices, indices);
	}

	Ref<Mesh> Mesh::CreateCube()
	{
		// 单位立方体，中心在原点，范围 [-0.5, 0.5]
		// 每个面 4 个顶点（独立顶点保证法线正确）
		std::vector<MeshVertex> vertices = {
			// +Z 前面  (法线 0,0,1, 切线 1,0,0)
			{{ -0.5f, -0.5f,  0.5f }, { 0,0,1 }, { 0,0 }, { 1,0,0 }},
			{{  0.5f, -0.5f,  0.5f }, { 0,0,1 }, { 1,0 }, { 1,0,0 }},
			{{  0.5f,  0.5f,  0.5f }, { 0,0,1 }, { 1,1 }, { 1,0,0 }},
			{{ -0.5f,  0.5f,  0.5f }, { 0,0,1 }, { 0,1 }, { 1,0,0 }},

			// -Z 后面  (法线 0,0,-1, 切线 -1,0,0)
			{{  0.5f, -0.5f, -0.5f }, { 0,0,-1 }, { 0,0 }, { -1,0,0 }},
			{{ -0.5f, -0.5f, -0.5f }, { 0,0,-1 }, { 1,0 }, { -1,0,0 }},
			{{ -0.5f,  0.5f, -0.5f }, { 0,0,-1 }, { 1,1 }, { -1,0,0 }},
			{{  0.5f,  0.5f, -0.5f }, { 0,0,-1 }, { 0,1 }, { -1,0,0 }},

			// -X 左面  (法线 -1,0,0, 切线 0,0,1)
			{{ -0.5f, -0.5f, -0.5f }, { -1,0,0 }, { 0,0 }, { 0,0,1 }},
			{{ -0.5f, -0.5f,  0.5f }, { -1,0,0 }, { 1,0 }, { 0,0,1 }},
			{{ -0.5f,  0.5f,  0.5f }, { -1,0,0 }, { 1,1 }, { 0,0,1 }},
			{{ -0.5f,  0.5f, -0.5f }, { -1,0,0 }, { 0,1 }, { 0,0,1 }},

			// +X 右面  (法线 1,0,0, 切线 0,0,-1)
			{{  0.5f, -0.5f,  0.5f }, { 1,0,0 }, { 0,0 }, { 0,0,-1 }},
			{{  0.5f, -0.5f, -0.5f }, { 1,0,0 }, { 1,0 }, { 0,0,-1 }},
			{{  0.5f,  0.5f, -0.5f }, { 1,0,0 }, { 1,1 }, { 0,0,-1 }},
			{{  0.5f,  0.5f,  0.5f }, { 1,0,0 }, { 0,1 }, { 0,0,-1 }},

			// +Y 上面  (法线 0,1,0, 切线 1,0,0)
			{{ -0.5f,  0.5f,  0.5f }, { 0,1,0 }, { 0,0 }, { 1,0,0 }},
			{{  0.5f,  0.5f,  0.5f }, { 0,1,0 }, { 1,0 }, { 1,0,0 }},
			{{  0.5f,  0.5f, -0.5f }, { 0,1,0 }, { 1,1 }, { 1,0,0 }},
			{{ -0.5f,  0.5f, -0.5f }, { 0,1,0 }, { 0,1 }, { 1,0,0 }},

			// -Y 下面  (法线 0,-1,0, 切线 1,0,0)
			{{ -0.5f, -0.5f, -0.5f }, { 0,-1,0 }, { 0,0 }, { 1,0,0 }},
			{{  0.5f, -0.5f, -0.5f }, { 0,-1,0 }, { 1,0 }, { 1,0,0 }},
			{{  0.5f, -0.5f,  0.5f }, { 0,-1,0 }, { 1,1 }, { 1,0,0 }},
			{{ -0.5f, -0.5f,  0.5f }, { 0,-1,0 }, { 0,1 }, { 1,0,0 }},
		};

		// 每面 2 个三角形，共 6 个面 × 6 = 36 个索引
		std::vector<uint32_t> indices;
		for (uint32_t face = 0; face < 6; face++)
		{
			uint32_t base = face * 4;
			indices.push_back(base + 0);
			indices.push_back(base + 1);
			indices.push_back(base + 2);
			indices.push_back(base + 2);
			indices.push_back(base + 3);
			indices.push_back(base + 0);
		}

		return CreateRef<Mesh>(vertices, indices);
	}
}
