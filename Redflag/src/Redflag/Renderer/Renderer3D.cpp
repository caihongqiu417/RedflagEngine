#include "rfpch.h"
#include "Renderer3D.h"
#include "Redflag/Renderer/Model.h"
#include "Redflag/Renderer/Material.h"

#include "Redflag/Renderer/RenderCommand.h"
#include "Redflag/Renderer/Shader.h"
#include "Redflag/Renderer/UniformBuffer.h"
#include "Redflag/Renderer/IBLPrecompute.h"  // IBL 预计算纹理绑定

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

		// std140: mat4(64) + vec4(16) + mat4(64) + vec4(16) + vec4(16) = 176 bytes
		// LightSpaceMatrix / ShadowParams / ShadowParams2 由 SceneRenderer 在
		// Shadow Pass 前后通过 SetShadowData 写入；主 Mesh shader 与 ShadowDepth
		// shader 共享这一个 UBO，避免每帧再开一条绑定点。
		struct CameraData
		{
			glm::mat4 ViewProjection;      // offset   0
			glm::vec4 ViewPosition;        // offset  64  (w = IBL ready flag)
			glm::mat4 LightSpaceMatrix;    // offset  80  光源正交裁剪矩阵
			glm::vec4 ShadowParams;        // offset 144  x=bias y=normalBias z=invMapSize w=mode
			glm::vec4 ShadowParams2;       // offset 160  x=pcfRadius y=lightSize z=_ w=_
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
			int       UseAlphaMap;         // offset 48
			float     AlphaCutoff;         // offset 52
			int       AlphaMode;           // offset 56  (0=Opaque, 1=Mask, 2=Blend)
			int       _pad0;               // offset 60  对齐到 16 字节
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
		s_Data.MaterialBuffer.UseAlphaMap            = mat->HasAlphaMap()             ? 1 : 0;
		s_Data.MaterialBuffer.AlphaCutoff            = mat->AlphaCutoff;
		s_Data.MaterialBuffer.AlphaMode              = static_cast<int>(mat->Alpha);
		s_Data.MaterialBuffer._pad0                  = 0;

		if (mat->HasBaseColorMap())        mat->BaseColorMap->Bind(3);
		if (mat->HasNormalMap())           mat->NormalMap->Bind(4);
		if (mat->HasMetallicRoughnessMap()) mat->MetallicRoughnessMap->Bind(5);
		if (mat->HasAOMap())               mat->AOMap->Bind(6);
		if (mat->HasAlphaMap())            mat->AlphaMap->Bind(10);
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
		// ViewPosition.w 被复用为 IBL 就绪标志（shader 根据它选择 IBL 或常数环境光）
		s_Data.CameraBuffer.ViewPosition   = glm::vec4(camera.GetPosition(),
			IBLPrecompute::IsReady() ? 1.0f : 0.0f);
		// Shadow 相关字段默认清零；SceneRenderer 在 Shadow Pass 前通过
		// SetShadowData 覆写它们。这里先填 0 是为了"无方向光"或"CastShadow=false"
		// 的场景下 shader 读到 mode=0，直接跳过阴影采样。
		s_Data.CameraBuffer.LightSpaceMatrix = glm::mat4(1.0f);
		s_Data.CameraBuffer.ShadowParams     = glm::vec4(0.0f);
		s_Data.CameraBuffer.ShadowParams2    = glm::vec4(1.5f, 6.0f, 0.0f, 0.0f);
		s_Data.CameraUniformBuffer->SetData(&s_Data.CameraBuffer, sizeof(Renderer3DData::CameraData));

		s_Data.DrawCommands.clear();
		ResetStats();
	}

	void Renderer3D::BeginScene(const glm::mat4& viewProjection, const glm::vec3& cameraPosition)
	{
		s_Data.CameraBuffer.ViewProjection = viewProjection;
		// ViewPosition.w 被复用为 IBL 就绪标志
		s_Data.CameraBuffer.ViewPosition   = glm::vec4(cameraPosition,
			IBLPrecompute::IsReady() ? 1.0f : 0.0f);
		s_Data.CameraBuffer.LightSpaceMatrix = glm::mat4(1.0f);
		s_Data.CameraBuffer.ShadowParams     = glm::vec4(0.0f);
		s_Data.CameraBuffer.ShadowParams2    = glm::vec4(1.5f, 6.0f, 0.0f, 0.0f);
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

		// 绑定 IBL 预计算纹理到 slot 7/8/9（所有 draw call 共享，只需绑一次）
		// 对应 shader: u_IrradianceMap / u_PrefilterMap / u_BRDFLUT
		IBLPrecompute::Bind();

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
	// SetShadowData — 更新 Camera UBO 中的光源空间矩阵 + 阴影参数
	//
	// 只写回 LightSpaceMatrix + ShadowParams + ShadowParams2 字段（96 字节），
	// 避免覆盖 BeginScene 里已经写好的 ViewProjection / ViewPosition。
	// -------------------------------------------------------------------
	void Renderer3D::SetShadowData(const glm::mat4& lightSpaceMatrix,
		const glm::vec4& shadowParams, const glm::vec4& shadowParams2)
	{
		s_Data.CameraBuffer.LightSpaceMatrix = lightSpaceMatrix;
		s_Data.CameraBuffer.ShadowParams     = shadowParams;
		s_Data.CameraBuffer.ShadowParams2    = shadowParams2;

		constexpr uint32_t offset = offsetof(Renderer3DData::CameraData, LightSpaceMatrix);
		constexpr uint32_t size   = sizeof(glm::mat4) + sizeof(glm::vec4) + sizeof(glm::vec4);
		s_Data.CameraUniformBuffer->SetData(&s_Data.CameraBuffer.LightSpaceMatrix, size, offset);
	}

	void Renderer3D::OverrideIBLFlag(bool enabled)
	{
		s_Data.CameraBuffer.ViewPosition.w = enabled ? 1.0f : 0.0f;
		constexpr uint32_t offset = offsetof(Renderer3DData::CameraData, ViewPosition);
		s_Data.CameraUniformBuffer->SetData(&s_Data.CameraBuffer.ViewPosition, sizeof(glm::vec4), offset);
	}

	// -------------------------------------------------------------------
	// ExecuteShadowPass — 用给定的深度 Shader 把当前队列里的几何全部画一遍
	//
	// 调用时机：SceneRenderer 在主 Pass (EndScene) 之前调用。
	// 前置条件：调用方已经：
	//   1. 绑定好 Shadow FBO 和对应视口
	//   2. 清理好深度（glClear(GL_DEPTH_BUFFER_BIT)）
	// 内部职责：
	//   - 绑定 depthShader
	//   - 按提交顺序逐个上传 Transform UBO（无需排序，没有材质切换开销）
	//   - 直接发出 DrawIndexed
	//
	// 不碰 CameraUBO：LightSpaceMatrix 由 SetShadowData 预先写入。
	// 不碰 MaterialUBO：shadow 深度不关心材质贴图。
	// -------------------------------------------------------------------
	void Renderer3D::ExecuteShadowPass(const Ref<Shader>& depthShader)
	{
		if (!depthShader || s_Data.DrawCommands.empty())
			return;

		depthShader->Bind();

		for (const auto& cmd : s_Data.DrawCommands)
		{
			if (!cmd.MeshData) continue;

			// 暂时跳过 Blend 材质（半透明玻璃等）避免错误地挡住阳光。
			// Opaque / Mask 材质都正常投射；Mask 的 alpha 裁剪我们这里
			// 暂不复现（代价是玻璃挂件等轮廓会投影成矩形）。
			if (cmd.Mat && cmd.Mat->Alpha == AlphaMode::Blend)
				continue;

			s_Data.TransformBuffer.Transform = cmd.Transform;
			s_Data.TransformUniformBuffer->SetData(&s_Data.TransformBuffer, sizeof(Renderer3DData::TransformData));

			cmd.MeshData->Bind();
			RenderCommand::DrawIndexed(cmd.MeshData->GetVertexArray(), cmd.MeshData->GetIndexCount());
		}
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

	void Renderer3D::DrawModel(const Ref<Model>& model, const glm::mat4& transform, const std::vector<Ref<Material>>& overrideMaterials, const glm::vec4& fallbackColor, int entityID)
	{
		if (!model) return;

		auto fallback = Material::Create(fallbackColor);
		const auto& subMeshes = model->GetSubMeshes();

		for (size_t i = 0; i < subMeshes.size(); i++)
		{
			const auto& subMesh = subMeshes[i];
			if (!subMesh.MeshData) continue;

			glm::mat4 finalTransform = transform * subMesh.LocalTransform;
			// per-submesh 覆盖材质 > 子网格自带材质 > 纯色回退
			Ref<Material> mat = (i < overrideMaterials.size() && overrideMaterials[i])
				? overrideMaterials[i]
				: (subMesh.Mat ? subMesh.Mat : fallback);
			s_Data.DrawCommands.push_back({ subMesh.MeshData, finalTransform, mat, entityID });
		}
	}
}
