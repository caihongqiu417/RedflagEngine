#include "rfpch.h"
#include "Renderer3D.h"
#include "Redflag/Renderer/Model.h"
#include "Redflag/Renderer/Material.h"

#include "Redflag/Renderer/RenderCommand.h"
#include "Redflag/Renderer/Shader.h"
#include "Redflag/Renderer/UniformBuffer.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

namespace Redflag
{
	// -------------------------------------------------------------------
	// 一条提交命令：一个 Mesh + 最终变换 + 材质
	// -------------------------------------------------------------------
	struct DrawCommand
	{
		Ref<Mesh>     MeshData;
		glm::mat4     Transform;
		Ref<Material> Mat;       // 为 nullptr 时使用默认白色纯色材质
	};

	struct Renderer3DData
	{
		Ref<Shader>        MeshShader;
		Ref<Texture2D>     WhiteTexture;
		Ref<Material>      DefaultMaterial; // 白色纯色

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

		// 每帧提交队列
		std::vector<DrawCommand> DrawCommands;

		// 统计
		uint32_t DrawCallCount = 0;
		Renderer3D::Statistics Stats;
	};

	static Renderer3DData s_Data;

	// -------------------------------------------------------------------
	// 内部：真正执行一次绘制（供 EndScene 调用）
	// -------------------------------------------------------------------
	static void ExecuteDraw(const DrawCommand& cmd)
	{
		// 上传变换
		s_Data.TransformBuffer.Transform = cmd.Transform;
		s_Data.TransformUniformBuffer->SetData(&s_Data.TransformBuffer, sizeof(Renderer3DData::TransformData));

		const Ref<Material>& mat = cmd.Mat ? cmd.Mat : s_Data.DefaultMaterial;

		if (mat->HasTexture())
		{
			s_Data.MaterialBuffer.Color  = mat->GetColor();
			s_Data.MaterialBuffer.Params = glm::vec4(1.0f); // UseTexture = 1
			s_Data.MaterialUniformBuffer->SetData(&s_Data.MaterialBuffer, sizeof(Renderer3DData::MaterialData));
			mat->GetTexture()->Bind(3);
		}
		else
		{
			s_Data.MaterialBuffer.Color  = mat->GetColor();
			s_Data.MaterialBuffer.Params = glm::vec4(0.0f); // UseTexture = 0
			s_Data.MaterialUniformBuffer->SetData(&s_Data.MaterialBuffer, sizeof(Renderer3DData::MaterialData));
		}

		cmd.MeshData->Bind();
		RenderCommand::DrawIndexed(cmd.MeshData->GetVertexArray(), cmd.MeshData->GetIndexCount());
		s_Data.DrawCallCount++;
	}

	void Renderer3D::Init()
	{
		s_Data.MeshShader = Shader::Create("assets/shaders/Renderer3D_Mesh.glsl");

		s_Data.WhiteTexture = Texture2D::Create(1, 1);
		uint32_t whiteData = 0xffffffff;
		s_Data.WhiteTexture->SetData(&whiteData, sizeof(uint32_t));

		s_Data.DefaultMaterial = Material::Create(glm::vec4(1.0f));

		s_Data.CameraUniformBuffer   = UniformBuffer::Create(sizeof(Renderer3DData::CameraData), 0);
		s_Data.TransformUniformBuffer = UniformBuffer::Create(sizeof(Renderer3DData::TransformData), 1);
		s_Data.MaterialUniformBuffer  = UniformBuffer::Create(sizeof(Renderer3DData::MaterialData), 2);

		s_Data.DrawCommands.reserve(1024);
		ResetStats();
	}

	void Renderer3D::Shutdown()
	{
		s_Data.MeshShader.reset();
		s_Data.WhiteTexture.reset();
		s_Data.DefaultMaterial.reset();
		s_Data.CameraUniformBuffer.reset();
		s_Data.TransformUniformBuffer.reset();
		s_Data.MaterialUniformBuffer.reset();
		s_Data.DrawCommands.clear();
	}

	void Renderer3D::BeginScene(const EditorCamera& camera)
	{
		s_Data.CameraBuffer.ViewProjection = camera.GetViewProjection();
		s_Data.CameraUniformBuffer->SetData(&s_Data.CameraBuffer, sizeof(Renderer3DData::CameraData));

		s_Data.DrawCommands.clear();
		ResetStats();
	}

	void Renderer3D::BeginScene(const glm::mat4& viewProjection)
	{
		s_Data.CameraBuffer.ViewProjection = viewProjection;
		s_Data.CameraUniformBuffer->SetData(&s_Data.CameraBuffer, sizeof(Renderer3DData::CameraData));

		s_Data.DrawCommands.clear();
		ResetStats();
	}

	void Renderer3D::EndScene()
	{
		s_Data.Stats.SubmittedCommands = static_cast<uint32_t>(s_Data.DrawCommands.size());
		s_Data.Stats.VertexCount = 0;
		s_Data.Stats.IndexCount = 0;
		for (const auto& cmd : s_Data.DrawCommands)
		{
			if (!cmd.MeshData)
				continue;
			s_Data.Stats.VertexCount += cmd.MeshData->GetVertexCount();
			s_Data.Stats.IndexCount += cmd.MeshData->GetIndexCount();
		}

		if (s_Data.DrawCommands.empty())
			return;

		// 按 SortKey（纹理指针）排序，相同材质的命令聚集在一起
		std::sort(s_Data.DrawCommands.begin(), s_Data.DrawCommands.end(),
			[](const DrawCommand& a, const DrawCommand& b)
			{
				uintptr_t ka = a.Mat ? a.Mat->GetSortKey() : 0;
				uintptr_t kb = b.Mat ? b.Mat->GetSortKey() : 0;
				return ka < kb;
			});

		s_Data.MeshShader->Bind();

		// 执行排序后的命令，相同材质只绑一次纹理/UBO
		uintptr_t lastKey = ~0ull;
		for (const auto& cmd : s_Data.DrawCommands)
		{
			const Ref<Material>& mat = cmd.Mat ? cmd.Mat : s_Data.DefaultMaterial;
			uintptr_t key = mat->GetSortKey();

			if (key != lastKey)
			{
				// 材质状态切换：重新上传 material UBO + 绑定纹理
				if (mat->HasTexture())
				{
					s_Data.MaterialBuffer.Color  = mat->GetColor();
					s_Data.MaterialBuffer.Params = glm::vec4(1.0f);
					mat->GetTexture()->Bind(3);
				}
				else
				{
					s_Data.MaterialBuffer.Color  = mat->GetColor();
					s_Data.MaterialBuffer.Params = glm::vec4(0.0f);
				}
				s_Data.MaterialUniformBuffer->SetData(&s_Data.MaterialBuffer, sizeof(Renderer3DData::MaterialData));
				lastKey = key;
			}

			// 上传逐 draw 的变换（每次必须更新）
			s_Data.TransformBuffer.Transform = cmd.Transform;
			s_Data.TransformUniformBuffer->SetData(&s_Data.TransformBuffer, sizeof(Renderer3DData::TransformData));

			cmd.MeshData->Bind();
			RenderCommand::DrawIndexed(cmd.MeshData->GetVertexArray(), cmd.MeshData->GetIndexCount());
			s_Data.DrawCallCount++;
		}
	}

	void Renderer3D::ResetStats()
	{
		s_Data.DrawCallCount = 0;
		s_Data.Stats = {};
	}

	Renderer3D::Statistics Renderer3D::GetStats()
	{
		s_Data.Stats.DrawCalls = s_Data.DrawCallCount;
		return s_Data.Stats;
	}

	// -------------------------------------------------------------------
	// 公开接口：推入队列
	// -------------------------------------------------------------------

	void Renderer3D::DrawMesh(const Ref<Mesh>& mesh, const glm::mat4& transform, const glm::vec4& color)
	{
		if (!mesh) return;
		s_Data.DrawCommands.push_back({ mesh, transform, Material::Create(color) });
	}

	void Renderer3D::DrawMesh(const Ref<Mesh>& mesh, const glm::mat4& transform, const Ref<Texture2D>& texture)
	{
		if (!mesh) return;
		s_Data.DrawCommands.push_back({ mesh, transform, Material::Create(texture) });
	}

	void Renderer3D::DrawMesh(const Ref<Mesh>& mesh, const glm::mat4& transform, const Ref<Material>& material)
	{
		if (!mesh) return;
		s_Data.DrawCommands.push_back({ mesh, transform, material });
	}

	void Renderer3D::DrawModel(const Ref<Model>& model, const glm::mat4& transform, const glm::vec4& color)
	{
		if (!model) return;

		// 创建一个共享的纯色材质供无材质子网格使用
		auto fallback = Material::Create(color);

		for (const auto& subMesh : model->GetSubMeshes())
		{
			if (!subMesh.MeshData) continue;

			glm::mat4 finalTransform = transform * subMesh.LocalTransform;
			Ref<Material> mat = subMesh.Mat ? subMesh.Mat : fallback;
			s_Data.DrawCommands.push_back({ subMesh.MeshData, finalTransform, mat });
		}
	}
}
