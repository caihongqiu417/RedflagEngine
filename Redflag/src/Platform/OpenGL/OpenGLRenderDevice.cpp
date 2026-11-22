#include "rfpch.h"
#include "OpenGLRenderDevice.h"
#include "OpenGLMeshGeometry.h"
#include "OpenGLTexture.h"
#include "Redflag/Renderer/Mesh.h"

namespace Redflag
{
	Ref<MeshGeometry> OpenGLRenderDevice::CreateMesh(const std::vector<MeshVertex>& vertices,
		const std::vector<uint32_t>& indices)
	{
		auto mesh = CreateRef<OpenGLMeshGeometry>();
		mesh->Array = VertexArray::Create();

		// 按 MeshVertex 的成员顺序告诉 OpenGL 去哪里取每项数据。
		const uint32_t bytes = static_cast<uint32_t>(vertices.size() * sizeof(MeshVertex));
		auto buffer = VertexBuffer::Create(bytes);
		buffer->SetData(vertices.data(), bytes);
		buffer->SetLayout({
			{ ShaderDataType::Float3, "a_Position" }, { ShaderDataType::Float3, "a_Normal" },
			{ ShaderDataType::Float2, "a_TexCoord" }, { ShaderDataType::Float3, "a_Tangent" },
			{ ShaderDataType::Float2, "a_EmissiveUVOffset" }
		});
		mesh->Array->AddVertexBuffer(buffer);

		// 顶点数组一起保留索引，稍后绑定网格就能找到两份数据。
		mesh->Array->SetIndexBuffer(IndexBuffer::Create(indices.data(), static_cast<uint32_t>(indices.size())));
		return mesh;
	}

	Ref<SampledTexture> OpenGLRenderDevice::CreateTexture(uint32_t width, uint32_t height, const void* rgba8)
	{
		auto texture = CreateRef<OpenGLTexture2D>(width, height);
		if (rgba8)
			texture->SetData(const_cast<void*>(rgba8), width * height * 4);

		return texture;
	}
}
