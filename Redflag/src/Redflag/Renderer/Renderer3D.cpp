#include "rfpch.h"
#include "Renderer3D.h"
#include "Redflag/Renderer/Model.h"

#include "Redflag/Renderer/RenderCommand.h"
#include "Redflag/Renderer/Shader.h"
#include "Redflag/Renderer/UniformBuffer.h"

#include <glm/gtc/matrix_transform.hpp>

namespace Redflag
{
	struct Renderer3DData
	{
		Ref<Shader>        MeshShader;
		Ref<Texture2D>     WhiteTexture;

		Ref<UniformBuffer> CameraUniformBuffer;    // binding = 0
		Ref<UniformBuffer> TransformUniformBuffer; // binding = 1
		Ref<UniformBuffer> MaterialUniformBuffer;  // binding = 2

		struct CameraData
		{
			glm::mat4 ViewProjection;
		};

		struct TransformData
		{
			glm::mat4 Transform;
		};

		// std140: vec4 + vec4 = 32 bytes
		struct MaterialData
		{
			glm::vec4 Color;
			glm::vec4 Params; // x = UseTexture
		};

		CameraData    CameraBuffer;
		TransformData TransformBuffer;
		MaterialData  MaterialBuffer;
	};

	static Renderer3DData s_Data;

	void Renderer3D::Init()
	{
		// 加载 Mesh Shader
		s_Data.MeshShader = Shader::Create("assets/shaders/Renderer3D_Mesh.glsl");

		// 创建 1x1 白色纹理（纯色绘制时使用）
		s_Data.WhiteTexture = Texture2D::Create(1, 1);
		uint32_t whiteData = 0xffffffff;
		s_Data.WhiteTexture->SetData(&whiteData, sizeof(uint32_t));

		// Camera UBO，binding = 0
		s_Data.CameraUniformBuffer   = UniformBuffer::Create(sizeof(Renderer3DData::CameraData), 0);
		// Transform UBO，binding = 1
		s_Data.TransformUniformBuffer = UniformBuffer::Create(sizeof(Renderer3DData::TransformData), 1);
		// Material UBO，binding = 2
		s_Data.MaterialUniformBuffer  = UniformBuffer::Create(sizeof(Renderer3DData::MaterialData), 2);
	}

	void Renderer3D::Shutdown()
	{
		s_Data.MeshShader.reset();
		s_Data.WhiteTexture.reset();
		s_Data.CameraUniformBuffer.reset();
		s_Data.TransformUniformBuffer.reset();
		s_Data.MaterialUniformBuffer.reset();
	}

	void Renderer3D::BeginScene(const EditorCamera& camera)
	{
		s_Data.CameraBuffer.ViewProjection = camera.GetViewProjection();
		s_Data.CameraUniformBuffer->SetData(&s_Data.CameraBuffer, sizeof(Renderer3DData::CameraData));
	}

	void Renderer3D::BeginScene(const glm::mat4& viewProjection)
	{
		s_Data.CameraBuffer.ViewProjection = viewProjection;
		s_Data.CameraUniformBuffer->SetData(&s_Data.CameraBuffer, sizeof(Renderer3DData::CameraData));
	}

	void Renderer3D::EndScene()
	{
		// 当前无批处理，留空供后续扩展
	}

	void Renderer3D::DrawMesh(const Ref<Mesh>& mesh, const glm::mat4& transform, const glm::vec4& color)
	{
		s_Data.TransformBuffer.Transform = transform;
		s_Data.TransformUniformBuffer->SetData(&s_Data.TransformBuffer, sizeof(Renderer3DData::TransformData));

		s_Data.MaterialBuffer.Color  = color;
		s_Data.MaterialBuffer.Params = glm::vec4(0.0f); // UseTexture = 0
		s_Data.MaterialUniformBuffer->SetData(&s_Data.MaterialBuffer, sizeof(Renderer3DData::MaterialData));

		s_Data.MeshShader->Bind();

		mesh->Bind();
		RenderCommand::DrawIndexed(mesh->GetVertexArray(), mesh->GetIndexCount());
	}

	void Renderer3D::DrawMesh(const Ref<Mesh>& mesh, const glm::mat4& transform, const Ref<Texture2D>& texture)
	{
		s_Data.TransformBuffer.Transform = transform;
		s_Data.TransformUniformBuffer->SetData(&s_Data.TransformBuffer, sizeof(Renderer3DData::TransformData));

		s_Data.MaterialBuffer.Params = glm::vec4(1.0f); // UseTexture = 1
		s_Data.MaterialUniformBuffer->SetData(&s_Data.MaterialBuffer, sizeof(Renderer3DData::MaterialData));

		s_Data.MeshShader->Bind();
		s_Data.MeshShader->SetInt("u_Texture", 3);

		texture->Bind(3);

		mesh->Bind();
		RenderCommand::DrawIndexed(mesh->GetVertexArray(), mesh->GetIndexCount());
	}

	void Renderer3D::DrawModel(const Ref<Model>& model, const glm::mat4& transform, const glm::vec4& color)
	{
		if (!model)
			return;

		for (const auto& subMesh : model->GetSubMeshes())
		{
			if (!subMesh.MeshData)
				continue;

			glm::mat4 finalTransform = transform * subMesh.LocalTransform;

			if (subMesh.DiffuseTexture)
				DrawMesh(subMesh.MeshData, finalTransform, subMesh.DiffuseTexture);
			else
				DrawMesh(subMesh.MeshData, finalTransform, color);
		}
	}
}
