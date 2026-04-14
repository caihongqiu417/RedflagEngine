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
		int           EntityID = -1;
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
			glm::vec4 ViewPosition;  // xyz = 相机世界坐标, w = unused
		};

		struct TransformData
		{
			glm::mat4 Transform;
		};

		// std140: vec4(16) + 4*float/int(16) + 4*int(16) = 48 bytes
		struct MaterialData
		{
			glm::vec4 BaseColor;           // offset 0
			float     Metallic;            // offset 16
			float     Roughness;           // offset 20
			float     AO;                  // offset 24
			int       UseBaseColorMap;     // offset 28
			int       UseNormalMap;        // offset 32
			int       UseMetallicRoughnessMap; // offset 36
			int       UseAOMap;            // offset 40
			int       EntityID;            // offset 44
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
	// -------------------------------------------------------------------
	// 填充 MaterialBuffer 并绑定贴图
	// -------------------------------------------------------------------
	static void UploadMaterial(const Ref<Material>& mat)
	{
		s_Data.MaterialBuffer.BaseColor  = mat->BaseColor;
		s_Data.MaterialBuffer.Metallic   = mat->Metallic;
		s_Data.MaterialBuffer.Roughness  = mat->Roughness;
		s_Data.MaterialBuffer.AO         = mat->AO;

		s_Data.MaterialBuffer.UseBaseColorMap        = mat->HasBaseColorMap()        ? 1 : 0;
		s_Data.MaterialBuffer.UseNormalMap            = mat->HasNormalMap()            ? 1 : 0;
		s_Data.MaterialBuffer.UseMetallicRoughnessMap = mat->HasMetallicRoughnessMap() ? 1 : 0;
		s_Data.MaterialBuffer.UseAOMap               = mat->HasAOMap()                ? 1 : 0;

		if (mat->HasBaseColorMap())        mat->BaseColorMap->Bind(3);
		if (mat->HasNormalMap())           mat->NormalMap->Bind(4);
		if (mat->HasMetallicRoughnessMap()) mat->MetallicRoughnessMap->Bind(5);
		if (mat->HasAOMap())               mat->AOMap->Bind(6);
	}

	static void ExecuteDraw(const DrawCommand& cmd)
	{
		// 上传变换
		s_Data.TransformBuffer.Transform = cmd.Transform;
		s_Data.TransformUniformBuffer->SetData(&s_Data.TransformBuffer, sizeof(Renderer3DData::TransformData));

		const Ref<Material>& mat = cmd.Mat ? cmd.Mat : s_Data.DefaultMaterial;
		UploadMaterial(mat);
		s_Data.MaterialBuffer.EntityID = cmd.EntityID;
		s_Data.MaterialUniformBuffer->SetData(&s_Data.MaterialBuffer, sizeof(Renderer3DData::MaterialData));

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
		s_Data.CameraBuffer.ViewPosition   = glm::vec4(camera.GetPosition(), 1.0f);
		s_Data.CameraUniformBuffer->SetData(&s_Data.CameraBuffer, sizeof(Renderer3DData::CameraData));

		s_Data.DrawCommands.clear();
		ResetStats();
	}

	void Renderer3D::BeginScene(const glm::mat4& viewProjection, const glm::vec3& cameraPosition)
	{
		s_Data.CameraBuffer.ViewProjection = viewProjection;
		s_Data.CameraBuffer.ViewPosition   = glm::vec4(cameraPosition, 1.0f);
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
				// 材质状态切换：重新上传 PBR material UBO + 绑定贴图
				UploadMaterial(mat);
				lastKey = key;
			}

			// 上传逐 draw 的变换（每次必须更新）
			s_Data.TransformBuffer.Transform = cmd.Transform;
			s_Data.TransformUniformBuffer->SetData(&s_Data.TransformBuffer, sizeof(Renderer3DData::TransformData));
			s_Data.MaterialBuffer.EntityID = cmd.EntityID;
			s_Data.MaterialUniformBuffer->SetData(&s_Data.MaterialBuffer, sizeof(Renderer3DData::MaterialData));

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

	void Renderer3D::DrawMesh(const Ref<Mesh>& mesh, const glm::mat4& transform, const glm::vec4& color, int entityID)
	{
		if (!mesh) return;
		s_Data.DrawCommands.push_back({ mesh, transform, Material::Create(color), entityID });
	}

	void Renderer3D::DrawMesh(const Ref<Mesh>& mesh, const glm::mat4& transform, const Ref<Texture2D>& texture, int entityID)
	{
		if (!mesh) return;
		s_Data.DrawCommands.push_back({ mesh, transform, Material::Create(texture), entityID });
	}

	void Renderer3D::DrawMesh(const Ref<Mesh>& mesh, const glm::mat4& transform, const Ref<Material>& material, int entityID)
	{
		if (!mesh) return;
		s_Data.DrawCommands.push_back({ mesh, transform, material, entityID });
	}

	void Renderer3D::DrawModel(const Ref<Model>& model, const glm::mat4& transform, const glm::vec4& color, int entityID)
	{
		if (!model) return;

		// 创建一个共享的纯色材质供无材质子网格使用
		auto fallback = Material::Create(color);

		for (const auto& subMesh : model->GetSubMeshes())
		{
			if (!subMesh.MeshData) continue;

			glm::mat4 finalTransform = transform * subMesh.LocalTransform;
			Ref<Material> mat = subMesh.Mat ? subMesh.Mat : fallback;
			s_Data.DrawCommands.push_back({ subMesh.MeshData, finalTransform, mat, entityID });
		}
	}

	void Renderer3D::DrawModel(const Ref<Model>& model, const glm::mat4& transform, const Ref<Material>& overrideMaterial, const glm::vec4& fallbackColor, int entityID)
	{
		if (!model) return;

		auto fallback = Material::Create(fallbackColor);

		for (const auto& subMesh : model->GetSubMeshes())
		{
			if (!subMesh.MeshData) continue;

			glm::mat4 finalTransform = transform * subMesh.LocalTransform;
			// 覆盖材质 > 子网格自带材质 > 纯色回退
			Ref<Material> mat = overrideMaterial ? overrideMaterial : (subMesh.Mat ? subMesh.Mat : fallback);
			s_Data.DrawCommands.push_back({ subMesh.MeshData, finalTransform, mat, entityID });
		}
	}
}
