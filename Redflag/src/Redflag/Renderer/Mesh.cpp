#include "rfpch.h"
#include "Mesh.h"

namespace Redflag
{
	Mesh::Mesh(const std::vector<MeshVertex>& vertices, const std::vector<uint32_t>& indices)
	{
		m_VertexCount = (uint32_t)vertices.size();
		m_IndexCount = (uint32_t)indices.size();

		m_VertexArray = VertexArray::Create();

		// 顶点缓冲
		Ref<VertexBuffer> vbo = VertexBuffer::Create((uint32_t)(vertices.size() * sizeof(MeshVertex)));
		vbo->SetData((void*)vertices.data(), (uint32_t)(vertices.size() * sizeof(MeshVertex)));

		vbo->SetLayout({
			{ ShaderDataType::Float3, "a_Position" },
			{ ShaderDataType::Float3, "a_Normal"   },
			{ ShaderDataType::Float2, "a_TexCoord" },
			{ ShaderDataType::Float3, "a_Tangent"  }
		});
		m_VertexArray->AddVertexBuffer(vbo);

		// 索引缓冲
		Ref<IndexBuffer> ibo = IndexBuffer::Create(
			(uint32_t*)indices.data(),
			(uint32_t)indices.size()
		);
		m_VertexArray->SetIndexBuffer(ibo);
	}

	void Mesh::Bind() const
	{
		m_VertexArray->Bind();
	}

	void Mesh::Unbind() const
	{
		m_VertexArray->Unbind();
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
