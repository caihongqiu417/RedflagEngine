#include "rfpch.h"
#include "Renderer3D.h"
#include "Redflag/Renderer/Model.h"
#include "Redflag/Renderer/Material.h"

#include "Redflag/Renderer/RenderCommand.h"
#include "Redflag/Renderer/Shader.h"
#include "Redflag/Renderer/UniformBuffer.h"
#include "Redflag/Renderer/IBLPrecompute.h"  // IBL 预计算纹理绑定
#include "Redflag/Renderer/Frustum.h"
#include "Redflag/Renderer/Renderer2D.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <functional>

namespace Redflag
{
	// ----------
	// 一条提交命令：一个 Mesh + 最终变换 + 材质
	// ----------
	struct DrawCommand
	{
		Ref<Mesh>     MeshData;
		glm::mat4     Transform;
		Ref<Material> Mat;       // 入队时已填充默认材质，Pass 内不再判断空引用
		int           EntityID = -1;
		bool          CameraCulled = false; // 只影响主相机绘制，仍可向画面内投影或反射光
		bool          Skip     = false; // 已由 Instancing Pass 处理，跳过常规路径
	};

	struct Renderer3DData
	{
		Ref<Shader>        MeshShader;
		Ref<Material>      DefaultMaterial; // 白色纯色

		Ref<UniformBuffer> CameraUniformBuffer;    // binding = 0
		Ref<UniformBuffer> TransformUniformBuffer; // binding = 1
		Ref<UniformBuffer> MaterialUniformBuffer;  // binding = 2

		// std140: mat4(64) + vec4(16) + mat4(64) + vec4(16) + vec4(16) + mat4(64) = 240 bytes
		// LightSpaceMatrix / ShadowParams / ShadowParams2 由 SceneRenderer 在
		// Shadow Pass 前后通过 SetShadowData 写入；主 Mesh shader 与 ShadowDepth
		// shader 共享这一个 UBO，避免每帧再开一条绑定点。
		// View：主相机世界→观察空间矩阵。SSAO Pass 和 Z-Prepass 需要把世界法线
		// 变换到 view space 写入法线附件。放在末尾保持已有字段的 offset 不变。
		struct CameraData
		{
			glm::mat4 ViewProjection;      // offset   0
			glm::vec4 ViewPosition;        // offset  64  (w = IBL ready flag)
			glm::mat4 LightSpaceMatrix;    // offset  80  光源正交裁剪矩阵
			glm::vec4 ShadowParams;        // offset 144  x=bias y=normalBias z=invMapSize w=mode
			glm::vec4 ShadowParams2;       // offset 160  x=pcfRadius y=lightSize z=viewportW w=viewportH
			glm::mat4 View;                // offset 176  主相机 view 矩阵（SSAO 用）
		};

		struct TransformData
		{
			glm::mat4 Transform;
		};

		// std140 共 64 字节，包含 AlphaMode 与末尾填充，与 Mesh Shader 的 Material 块一致。
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

		// ----------
		// GPU Instancing 资源
		//
		// 每帧在 EndScene 里将同 Mesh、同贴图组合的 Opaque 命令批量写入
		// InstanceVBO，一次 glDrawElementsInstanced 替代 N 个独立 draw call。
		//
		// 实例数据布局（与 Renderer3D_Mesh.glsl / INSTANCED 的 layout 对应）：
		//   location 4-7 : mat4  Transform   (64 bytes)
		//   location 8   : vec4  BaseColor   (16 bytes)
		//   location 9   : vec4  MatParams   (x=metallic, y=roughness, z=ao, w=0)
		//   location 10  : int   EntityID    (4 bytes)
		// ----------
		struct InstanceData
		{
			glm::mat4 Transform;    // offset   0, 64 bytes
			glm::vec4 BaseColor;    // offset  64, 16 bytes
			glm::vec4 MatParams;    // offset  80, 16 bytes
			int       EntityID;     // offset  96,  4 bytes
			int       _pad[3];      // offset 100, 12 bytes → stride = 112
		};

		static constexpr uint32_t MAX_INSTANCES = 2048;

		Ref<Shader>  InstancedMeshShader;
		uint32_t     InstanceVBO = 0;

		// 每帧提交队列
		std::vector<DrawCommand> DrawCommands;

		// 当前帧视锥体（BeginScene 时构建，DrawMesh/DrawModel 使用）
		Frustum CameraFrustum;

		// 剪裁可视化用：世界空间 AABB + 是否被剪掉
		struct DebugAABB { glm::vec3 Min; glm::vec3 Max; bool Culled; };
		std::vector<DebugAABB> DebugAABBs;

		// 每帧 DrawCall 日志（供 ImGui 面板展示）
		std::vector<Renderer3D::DrawCallInfo> DrawCallLog;

		// 统计
		uint32_t DrawCallCount = 0;
		Renderer3D::Statistics Stats;
	};

	static Renderer3DData s_Data;

	// 视锥体剪裁开关（默认开启）
	static bool s_FrustumCullingEnabled = true;
	// 剪裁可视化开关（默认关闭）
	static bool s_DebugCullingViz = false;

	bool& Renderer3D::GetFrustumCullingEnabled()    { return s_FrustumCullingEnabled; }
	bool& Renderer3D::GetDebugCullingVizEnabled()  { return s_DebugCullingViz; }

	// ---- 材质与单次绘制 ----
	// 填充 CPU 侧材质数据并绑定贴图；EntityID 与 UBO 上传由每次绘制补齐。
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

	// ---- 初始化与销毁 ----
	void Renderer3D::Init()
	{
		s_Data.MeshShader          = Shader::Create("assets/shaders/Renderer3D_Mesh.glsl");
		s_Data.InstancedMeshShader = Shader::Create("assets/shaders/Renderer3D_Mesh.glsl", "INSTANCED");

		glCreateBuffers(1, &s_Data.InstanceVBO);
		glNamedBufferData(s_Data.InstanceVBO,
			Renderer3DData::MAX_INSTANCES * (GLsizeiptr)sizeof(Renderer3DData::InstanceData),
			nullptr, GL_DYNAMIC_DRAW);

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
		s_Data.InstancedMeshShader.reset();
		if (s_Data.InstanceVBO) { glDeleteBuffers(1, &s_Data.InstanceVBO); s_Data.InstanceVBO = 0; }
		s_Data.DefaultMaterial.reset();
		s_Data.CameraUniformBuffer.reset();
		s_Data.TransformUniformBuffer.reset();
		s_Data.MaterialUniformBuffer.reset();
		s_Data.DrawCommands.clear();
	}

	// ---- 帧状态 ----
	void Renderer3D::BeginScene(const EditorCamera& camera)
	{
		s_Data.CameraBuffer.ViewProjection = camera.GetViewProjection();
		s_Data.CameraBuffer.View           = camera.GetViewMatrix();
		// ViewPosition.w 被复用为 IBL 就绪标志（shader 根据它选择 IBL 或常数环境光）
		s_Data.CameraBuffer.ViewPosition   = glm::vec4(camera.GetPosition(),
			IBLPrecompute::IsReady() ? 1.0f : 0.0f);
		// Shadow 相关字段默认清零；SceneRenderer 在 Shadow Pass 前通过
		// SetShadowData 覆写它们。这里先填 0 是为了"无方向光"或"CastShadow=false"
		// 的场景下 shader 读到 mode=0，直接跳过阴影采样。
		s_Data.CameraBuffer.LightSpaceMatrix = glm::mat4(1.0f);
		s_Data.CameraBuffer.ShadowParams     = glm::vec4(0.0f);
		// ShadowParams2.zw 复用为当前视口像素尺寸（供主 Mesh shader
		// 在 fragment 里 gl_FragCoord.xy / viewportSize 采样 SSAO map）
		GLint vp[4]; glGetIntegerv(GL_VIEWPORT, vp);
		s_Data.CameraBuffer.ShadowParams2    = glm::vec4(1.5f, 6.0f, float(vp[2]), float(vp[3]));
		s_Data.CameraUniformBuffer->SetData(&s_Data.CameraBuffer, sizeof(Renderer3DData::CameraData));

		// 构建当帧视锥体（供 DrawMesh/DrawModel 做 AABB 剪裁）
		s_Data.CameraFrustum = Frustum::FromViewProjection(s_Data.CameraBuffer.ViewProjection);

		s_Data.DrawCommands.clear();
		s_Data.DebugAABBs.clear();
		s_Data.DrawCallLog.clear();
		ResetStats();
	}

	void Renderer3D::BeginScene(const glm::mat4& viewProjection, const glm::vec3& cameraPosition, const glm::mat4& view)
	{
		s_Data.CameraBuffer.ViewProjection = viewProjection;
		s_Data.CameraBuffer.View           = view;
		// ViewPosition.w 被复用为 IBL 就绪标志
		s_Data.CameraBuffer.ViewPosition   = glm::vec4(cameraPosition,
			IBLPrecompute::IsReady() ? 1.0f : 0.0f);
		s_Data.CameraBuffer.LightSpaceMatrix = glm::mat4(1.0f);
		s_Data.CameraBuffer.ShadowParams     = glm::vec4(0.0f);
		GLint vp[4]; glGetIntegerv(GL_VIEWPORT, vp);
		s_Data.CameraBuffer.ShadowParams2    = glm::vec4(1.5f, 6.0f, float(vp[2]), float(vp[3]));
		s_Data.CameraUniformBuffer->SetData(&s_Data.CameraBuffer, sizeof(Renderer3DData::CameraData));

		s_Data.CameraFrustum = Frustum::FromViewProjection(viewProjection);

		s_Data.DrawCommands.clear();
		s_Data.DebugAABBs.clear();
		s_Data.DrawCallLog.clear();
		ResetStats();
	}

	// ---- 主 Pass ----
	// 先绘制可实例化的 Opaque 分组，再按材质对象排序绘制剩余队列。
	void Renderer3D::EndScene()
	{
		RF_PROFILE_FUNCTION();

		// 光源 Pass 已使用完整队列；主画面只保留主相机可见的命令。
		s_Data.DrawCommands.erase(
			std::remove_if(s_Data.DrawCommands.begin(), s_Data.DrawCommands.end(),
				[](const DrawCommand& command) { return command.CameraCulled; }),
			s_Data.DrawCommands.end());

		s_Data.Stats.SubmittedCommands = static_cast<uint32_t>(s_Data.DrawCommands.size());
		s_Data.Stats.VertexCount = 0;
		s_Data.Stats.IndexCount = 0;
		for (const auto& cmd : s_Data.DrawCommands)
		{
			s_Data.Stats.VertexCount += cmd.MeshData->GetVertexCount();
			s_Data.Stats.IndexCount += cmd.MeshData->GetIndexCount();
		}

		if (s_Data.DrawCommands.empty())
			return;

		// ----------
		// GPU Instancing Pass
		//
		// 条件：同一 MeshData 指针、全部贴图指针相同、Opaque，且同组数量 >= 2。
		// 这类命令打包进 InstanceVBO，一次 glDrawElementsInstanced 替代 N 个 draw。
		// Shadow Pass / Depth Prepass 在 EndScene 之前已执行，仍走逐个 draw（保证阴影正确）。
		// ----------
		if (s_Data.InstancedMeshShader && s_Data.InstanceVBO)
		{
			// ---- 收集可合批候选（所有 Opaque 命令，含贴图）----
			// 分组键：(Mesh, BaseColorMap, NormalMap, MetallicRoughnessMap, AOMap, AlphaMap)
			// 相同 Mesh + 完全相同的贴图组合 → 可以合批实例化绘制
			struct Candidate
			{
				size_t CommandIndex;
				// 顺序固定为网格、基础色、法线、金属粗糙度、AO、透明度贴图。
				std::array<const void*, 6> Key;
			};
			std::vector<Candidate> candidates;
			candidates.reserve(s_Data.DrawCommands.size());

			for (size_t i = 0; i < s_Data.DrawCommands.size(); i++)
			{
				const auto& cmd = s_Data.DrawCommands[i];
				const Ref<Material>& mat = cmd.Mat;
				if (mat->Alpha == AlphaMode::Opaque)
				{
					candidates.push_back({ i, { cmd.MeshData.get(),
						mat->BaseColorMap.get(), mat->NormalMap.get(),
						mat->MetallicRoughnessMap.get(), mat->AOMap.get(),
						mat->AlphaMap.get() } });
				}
			}

			// 按 (Mesh, textures) 排序，使同组候选相邻
			std::sort(candidates.begin(), candidates.end(),
				[](const Candidate& a, const Candidate& b)
				{
					return std::lexicographical_compare(a.Key.begin(), a.Key.end(),
						b.Key.begin(), b.Key.end(), std::less<const void*>());
				});

			// ---- 遍历分组，组内 >=2 则走 Instancing ----
			bool didInstance = false;

			s_Data.InstancedMeshShader->Bind();
			IBLPrecompute::Bind();

			static std::vector<Renderer3DData::InstanceData> instBuf;

			size_t grpStart = 0;
			while (grpStart < candidates.size())
			{
				// 找到 (Mesh + 全部贴图) 完全相同的连续区间 [grpStart, grpEnd)
				size_t grpEnd = grpStart;
				while (grpEnd < candidates.size() &&
					candidates[grpEnd].Key == candidates[grpStart].Key)
					grpEnd++;

				size_t grpSize = grpEnd - grpStart;

				if (grpSize >= 2)
				{
					didInstance = true;
					const Ref<Mesh>& mesh = s_Data.DrawCommands[candidates[grpStart].CommandIndex].MeshData;

					// 标记所有组内命令跳过常规路径
					for (size_t k = grpStart; k < grpEnd; k++)
						s_Data.DrawCommands[candidates[k].CommandIndex].Skip = true;

					// 上传 per-batch 材质状态（贴图 UseXxx 标志 + 绑定贴图）
					// 组内所有实例共用同一套贴图，只需上传一次
					const Ref<Material>& batchMat = s_Data.DrawCommands[candidates[grpStart].CommandIndex].Mat;
					UploadMaterial(batchMat);
					s_Data.MaterialBuffer.EntityID = 0; // instanced path 通过 v_EntityID 传递
					s_Data.MaterialUniformBuffer->SetData(&s_Data.MaterialBuffer, sizeof(Renderer3DData::MaterialData));

					// 分批：每批最多 MAX_INSTANCES 个实例
					size_t offset = 0;
					while (offset < grpSize)
					{
						size_t batchCount = std::min(grpSize - offset, (size_t)Renderer3DData::MAX_INSTANCES);
						instBuf.resize(batchCount);

						for (size_t k = 0; k < batchCount; k++)
						{
							const auto& cmd = s_Data.DrawCommands[candidates[grpStart + offset + k].CommandIndex];
							const Ref<Material>& mat = cmd.Mat;
							instBuf[k].Transform = cmd.Transform;
							instBuf[k].BaseColor = mat->BaseColor;
							instBuf[k].MatParams = glm::vec4(mat->Metallic, mat->Roughness, mat->AO, 0.0f);
							instBuf[k].EntityID  = cmd.EntityID;
							instBuf[k]._pad[0] = instBuf[k]._pad[1] = instBuf[k]._pad[2] = 0;
						}

						glNamedBufferSubData(s_Data.InstanceVBO, 0,
							(GLsizeiptr)(batchCount * sizeof(Renderer3DData::InstanceData)),
							instBuf.data());

						// 绑定 Mesh VAO，然后在其上附加实例 VBO 属性
						mesh->Bind();
						glBindBuffer(GL_ARRAY_BUFFER, s_Data.InstanceVBO);
						constexpr GLsizei stride = (GLsizei)sizeof(Renderer3DData::InstanceData);

						// Transform: mat4 = 4 × vec4，locations 4-7，divisor=1
						for (int col = 0; col < 4; col++)
						{
							glEnableVertexAttribArray(4 + col);
							glVertexAttribPointer(4 + col, 4, GL_FLOAT, GL_FALSE, stride,
								(const void*)(offsetof(Renderer3DData::InstanceData, Transform) + col * sizeof(glm::vec4)));
							glVertexAttribDivisor(4 + col, 1);
						}
						// BaseColor: location 8，divisor=1
						glEnableVertexAttribArray(8);
						glVertexAttribPointer(8, 4, GL_FLOAT, GL_FALSE, stride,
							(const void*)offsetof(Renderer3DData::InstanceData, BaseColor));
						glVertexAttribDivisor(8, 1);
						// MatParams: location 9，divisor=1
						glEnableVertexAttribArray(9);
						glVertexAttribPointer(9, 4, GL_FLOAT, GL_FALSE, stride,
							(const void*)offsetof(Renderer3DData::InstanceData, MatParams));
						glVertexAttribDivisor(9, 1);
						// EntityID: location 10，integer，divisor=1
						glEnableVertexAttribArray(10);
						glVertexAttribIPointer(10, 1, GL_INT, stride,
							(const void*)offsetof(Renderer3DData::InstanceData, EntityID));
						glVertexAttribDivisor(10, 1);

						glDrawElementsInstanced(GL_TRIANGLES,
							(GLsizei)mesh->GetIndexCount(), GL_UNSIGNED_INT,
							nullptr, (GLsizei)batchCount);
						s_Data.DrawCallCount++;

						s_Data.DrawCallLog.push_back({
							Renderer3D::DrawCallInfo::PassType::Main,
							mesh->GetIndexCount(),
							s_Data.DrawCommands[candidates[grpStart + offset].CommandIndex].EntityID,
							(int)AlphaMode::Opaque, 0,
							reinterpret_cast<uintptr_t>(mesh.get()) });

						// 清理实例属性，避免污染该 VAO 的后续非 instanced 使用
						for (int loc = 4; loc <= 10; loc++)
							glDisableVertexAttribArray(loc);

						offset += batchCount;
					}
				}

				grpStart = grpEnd;
			}

			// 从队列移除已合批的命令，让常规路径只处理剩余命令
			if (didInstance)
			{
				s_Data.DrawCommands.erase(
					std::remove_if(s_Data.DrawCommands.begin(), s_Data.DrawCommands.end(),
						[](const DrawCommand& c) { return c.Skip; }),
					s_Data.DrawCommands.end());
			}

			if (s_Data.DrawCommands.empty())
				return;
		}

		// 按 SortKey（材质指针）排序，相同材质的命令聚集在一起
		std::sort(s_Data.DrawCommands.begin(), s_Data.DrawCommands.end(),
			[](const DrawCommand& a, const DrawCommand& b)
			{
				uintptr_t ka = a.Mat->GetSortKey();
				uintptr_t kb = b.Mat->GetSortKey();
				return ka < kb;
			});

		s_Data.MeshShader->Bind();

		// 绑定 IBL 预计算纹理到 slot 7/8/9（所有 draw call 共享，只需绑一次）
		// 对应 shader: u_IrradianceMap / u_PrefilterMap / u_BRDFLUT
		IBLPrecompute::Bind();

		// 同材质复用贴图绑定和CPU参数；EntityID 随命令变化，材质 UBO 仍每次上传。
		uintptr_t lastKey = ~0ull;
		for (const auto& cmd : s_Data.DrawCommands)
		{

			const Ref<Material>& mat = cmd.Mat;
			uintptr_t key = mat->GetSortKey();

			if (key != lastKey)
			{
				// 材质切换时更新 CPU 参数并绑定贴图，下面统一上传 UBO。
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

			s_Data.DrawCallLog.push_back({ Renderer3D::DrawCallInfo::PassType::Main,
				cmd.MeshData->GetIndexCount(), cmd.EntityID,
				static_cast<int>(mat->Alpha), key,
				reinterpret_cast<uintptr_t>(cmd.MeshData.get()) });
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

	const std::vector<Renderer3D::DrawCallInfo>& Renderer3D::GetDrawCallLog()
	{
		return s_Data.DrawCallLog;
	}

	void Renderer3D::LogExternalDrawCall(DrawCallInfo::PassType pass, uint32_t indexCount)
	{
		// 供 SSAO 等屏幕空间 Pass 登记。没有 Mesh / Material 概念，字段置 0。
		s_Data.DrawCallCount++;
		s_Data.DrawCallLog.push_back({ pass, indexCount, -1, 0, 0, 0 });
	}

	// ----------
	// DrawDebugAABBs：用 Renderer2D::DrawLine 把这一帧收集到的
	//   世界空间 AABB 画成线框。调用方必须在外层已经 Renderer2D::BeginScene。
	//   绿色  = 通过剪裁（本帧实际绘制了）
	//   红色  = 主相机剔除（仍参与阴影和 RSM）
	// 每个 AABB 画 12 条边。
	// ----------
	void Renderer3D::DrawDebugAABBs()
	{
		const glm::vec4 kept   (0.2f, 1.0f, 0.2f, 1.0f);
		const glm::vec4 culled (1.0f, 0.25f, 0.25f, 1.0f);

		for (const auto& box : s_Data.DebugAABBs)
		{
			const glm::vec4& c = box.Culled ? culled : kept;

			// 8 个角点
			glm::vec3 v[8] = {
				{ box.Min.x, box.Min.y, box.Min.z },
				{ box.Max.x, box.Min.y, box.Min.z },
				{ box.Max.x, box.Max.y, box.Min.z },
				{ box.Min.x, box.Max.y, box.Min.z },
				{ box.Min.x, box.Min.y, box.Max.z },
				{ box.Max.x, box.Min.y, box.Max.z },
				{ box.Max.x, box.Max.y, box.Max.z },
				{ box.Min.x, box.Max.y, box.Max.z },
			};

			// 底面 4 条
			Renderer2D::DrawLine(v[0], v[1], c);
			Renderer2D::DrawLine(v[1], v[2], c);
			Renderer2D::DrawLine(v[2], v[3], c);
			Renderer2D::DrawLine(v[3], v[0], c);
			// 顶面 4 条
			Renderer2D::DrawLine(v[4], v[5], c);
			Renderer2D::DrawLine(v[5], v[6], c);
			Renderer2D::DrawLine(v[6], v[7], c);
			Renderer2D::DrawLine(v[7], v[4], c);
			// 立柱 4 条
			Renderer2D::DrawLine(v[0], v[4], c);
			Renderer2D::DrawLine(v[1], v[5], c);
			Renderer2D::DrawLine(v[2], v[6], c);
			Renderer2D::DrawLine(v[3], v[7], c);
		}
	}

	// ----------
	// SetShadowData — 更新 Camera UBO 中的光源空间矩阵 + 阴影参数
	//
	// 只写回 LightSpaceMatrix + ShadowParams + ShadowParams2 字段（96 字节），
	// 避免覆盖 BeginScene 里已经写好的 ViewProjection / ViewPosition。
	// ----------
	void Renderer3D::SetShadowData(const glm::mat4& lightSpaceMatrix,
		const glm::vec4& shadowParams, const glm::vec4& shadowParams2)
	{
		s_Data.CameraBuffer.LightSpaceMatrix = lightSpaceMatrix;
		s_Data.CameraBuffer.ShadowParams     = shadowParams;
		// 只覆盖 xy（pcfRadius / lightSize），zw 保留 BeginScene 写入的视口尺寸
		// （SSAO 采样用 gl_FragCoord.xy / viewport）。上层调用时 zw 若不是 0
		// 也会被保留下面 BeginScene 的值覆盖。
		s_Data.CameraBuffer.ShadowParams2.x  = shadowParams2.x;
		s_Data.CameraBuffer.ShadowParams2.y  = shadowParams2.y;

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

	// ----------
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
	// Mask 使用与主画面相同的透明度裁剪，避免镂空表面投出实心阴影。
	// ----------
	void Renderer3D::ExecuteShadowPass(const Ref<Shader>& depthShader)
	{
		RF_PROFILE_FUNCTION();

		if (!depthShader || s_Data.DrawCommands.empty())
			return;

		depthShader->Bind();

		for (const auto& cmd : s_Data.DrawCommands)
		{

			// Blend 仍未实现透射阴影；屏幕外的 Opaque / Mask 也参与光源绘制。
			if (cmd.Mat->Alpha == AlphaMode::Blend)
				continue;

			s_Data.TransformBuffer.Transform = cmd.Transform;
			s_Data.TransformUniformBuffer->SetData(&s_Data.TransformBuffer, sizeof(Renderer3DData::TransformData));

			if (cmd.Mat->Alpha == AlphaMode::Mask)
				UploadMaterial(cmd.Mat);
			else
				s_Data.MaterialBuffer.AlphaMode = 0;
			s_Data.MaterialUniformBuffer->SetData(&s_Data.MaterialBuffer, sizeof(Renderer3DData::MaterialData));

			cmd.MeshData->Bind();
			RenderCommand::DrawIndexed(cmd.MeshData->GetVertexArray(), cmd.MeshData->GetIndexCount());
			s_Data.DrawCallCount++; // Shadow Pass 也是真实的 GL Draw

			const Ref<Material>& smat = cmd.Mat;
			s_Data.DrawCallLog.push_back({ Renderer3D::DrawCallInfo::PassType::Shadow,
				cmd.MeshData->GetIndexCount(), cmd.EntityID,
				static_cast<int>(smat->Alpha), smat->GetSortKey(),
				reinterpret_cast<uintptr_t>(cmd.MeshData.get()) });
		}
	}

	// ----------
	// ExecuteRSMPass — 用 RSM Depth shader 把当前队列里的几何重绘一次
	//
	// 与 ExecuteShadowPass 的区别：
	//   - 同时上传 Material UBO（RSM 片元需要 BaseColor / 贴图开关）
	//   - 同时绑定 BaseColorMap 等纹理（slot 3/4/5/6/10，复用 UploadMaterial）
	//   - 每个 cmd 仍然跳过 Blend 材质（与 Shadow Pass 一致）
	//
	// 调用方负责：
	//   - 绑定 RSM FBO（3 颜色 MRT + 深度）和对应视口
	//   - 清掉 RSM FBO 的颜色 + 深度
	//   - SetShadowData 写好光源空间矩阵
	// ----------
	void Renderer3D::ExecuteRSMPass(const Ref<Shader>& rsmShader)
	{
		RF_PROFILE_FUNCTION();

		if (!rsmShader || s_Data.DrawCommands.empty())
			return;

		rsmShader->Bind();

		for (const auto& cmd : s_Data.DrawCommands)
		{
			if (cmd.Mat->Alpha == AlphaMode::Blend)
				continue;

			// Transform UBO（每个 cmd 都要上传）
			s_Data.TransformBuffer.Transform = cmd.Transform;
			s_Data.TransformUniformBuffer->SetData(&s_Data.TransformBuffer, sizeof(Renderer3DData::TransformData));

			// 材质同时决定反射颜色和 Mask 覆盖率，与主画面的透明度相乘规则一致。
			const Ref<Material>& mat = cmd.Mat;
			UploadMaterial(mat);
			s_Data.MaterialBuffer.EntityID = cmd.EntityID;
			s_Data.MaterialUniformBuffer->SetData(&s_Data.MaterialBuffer, sizeof(Renderer3DData::MaterialData));

			cmd.MeshData->Bind();
			RenderCommand::DrawIndexed(cmd.MeshData->GetVertexArray(), cmd.MeshData->GetIndexCount());
			s_Data.DrawCallCount++;

			s_Data.DrawCallLog.push_back({ Renderer3D::DrawCallInfo::PassType::RSM,
				cmd.MeshData->GetIndexCount(), cmd.EntityID,
				static_cast<int>(mat->Alpha), mat->GetSortKey(),
				reinterpret_cast<uintptr_t>(cmd.MeshData.get()) });
		}
	}

	// ----------
	// ExecuteDepthPrepass — Z-Prepass，向主帧缓冲的深度缓冲写入深度
	//
	// 与 ExecuteShadowPass 的区别：
	//   - 目标是主帧缓冲（调用方不需要切换 FBO）
	//   - 使用主相机 ViewProjection（BeginScene 已写入 Camera UBO）
	//   - 仅关闭颜色0/1写入，颜色2存观察空间法线，供SSAO和RSM读取
	//   - Mask 材质上传颜色因子、基础色贴图和 AlphaMap，保持主画面的覆盖率
	//   - Blend 材质跳过（透明物体深度不确定，不参与 prepass）
	// ----------
	void Renderer3D::ExecuteDepthPrepass(const Ref<Shader>& depthShader)
	{
		RF_PROFILE_FUNCTION();

		if (!depthShader || s_Data.DrawCommands.empty())
			return;

		// 按物体原点到相机的平方距离由近到远排序，尽量让近处几何先写深度。
		// dist²=|objectPos-cameraPos|²；这是物体级近似，不能保证每个像素只写一次。
		const glm::vec3 camPos = glm::vec3(s_Data.CameraBuffer.ViewPosition);
		std::sort(s_Data.DrawCommands.begin(), s_Data.DrawCommands.end(),
			[&camPos](const DrawCommand& a, const DrawCommand& b)
			{
				glm::vec3 pa = glm::vec3(a.Transform[3]);
				glm::vec3 pb = glm::vec3(b.Transform[3]);
				float da = glm::dot(pa - camPos, pa - camPos);
				float db = glm::dot(pb - camPos, pb - camPos);
				return da < db;  // 近的排前面
			});

		// Z-Prepass 除了写深度，还顺便把 view-space 法线写到主 FBO 的
		// attachment2（RGBA16F）供 SSAO Pass 使用。关闭 attachment0/1 的写入
		// 避免污染颜色 / Entity ID。如果主 FBO 没有 attachment2（例如测试用的
		// 小 FBO），glColorMaski 设在不存在的 buffer 上是无害的。
		glColorMaski(0, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
		glColorMaski(1, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
		glColorMaski(2, GL_TRUE,  GL_TRUE,  GL_TRUE,  GL_TRUE);

		depthShader->Bind();

		for (const auto& cmd : s_Data.DrawCommands)
		{

			if (cmd.CameraCulled)
				continue;

			// Blend 材质跳过（半透明物体不应参与深度预写）
			const Ref<Material>& mat = cmd.Mat;
			if (mat->Alpha == AlphaMode::Blend)
				continue;

			// 上传逐物体变换
			s_Data.TransformBuffer.Transform = cmd.Transform;
			s_Data.TransformUniformBuffer->SetData(&s_Data.TransformBuffer, sizeof(Renderer3DData::TransformData));

			// Mask 与主 Pass 使用相同透明度：BaseColor.a × BaseColorMap.a × AlphaMap.r。
			if (mat->Alpha == AlphaMode::Mask)
			{
				UploadMaterial(mat);
				s_Data.MaterialUniformBuffer->SetData(&s_Data.MaterialBuffer, sizeof(Renderer3DData::MaterialData));
			}
			else
			{
				// Opaque：把 AlphaMode 写 0，shader 直接跳过 discard 逻辑
				s_Data.MaterialBuffer.AlphaMode = 0;
				s_Data.MaterialUniformBuffer->SetData(&s_Data.MaterialBuffer, sizeof(Renderer3DData::MaterialData));
			}

			cmd.MeshData->Bind();
			RenderCommand::DrawIndexed(cmd.MeshData->GetVertexArray(), cmd.MeshData->GetIndexCount());
			s_Data.DrawCallCount++; // Z-Prepass 也是真实的 GL Draw

			s_Data.DrawCallLog.push_back({ Renderer3D::DrawCallInfo::PassType::Prepass,
				cmd.MeshData->GetIndexCount(), cmd.EntityID,
				static_cast<int>(mat->Alpha), mat->GetSortKey(),
				reinterpret_cast<uintptr_t>(cmd.MeshData.get()) });
		}

		// 恢复全部颜色写入
		glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glColorMaski(1, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glColorMaski(2, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	}

	// ----------
	// 视锥体剪裁：把 Mesh 本地 AABB 变换到世界空间后测试
	//   用 Arvo 法：worldCenter = T * localCenter
	//                   worldExtent = |mat3(T)| * localExtent
	//   比变换 8 个角点再重新求 min/max 快很多。
	// ----------
	static bool CullMesh(const Mesh& mesh, const glm::mat4& transform)
	{
		// 总是先算世界空间 AABB（可视化 / 剪裁判断都要用）
		const glm::vec3& lmin = mesh.GetAABBMin();
		const glm::vec3& lmax = mesh.GetAABBMax();
		glm::vec3 lCenter = 0.5f * (lmin + lmax);
		glm::vec3 lExtent = 0.5f * (lmax - lmin);

		glm::vec3 wCenter = glm::vec3(transform * glm::vec4(lCenter, 1.0f));

		glm::mat3 absM(
			glm::abs(transform[0].x), glm::abs(transform[0].y), glm::abs(transform[0].z),
			glm::abs(transform[1].x), glm::abs(transform[1].y), glm::abs(transform[1].z),
			glm::abs(transform[2].x), glm::abs(transform[2].y), glm::abs(transform[2].z));
		glm::vec3 wExtent = absM * lExtent;

		glm::vec3 wMin = wCenter - wExtent;
		glm::vec3 wMax = wCenter + wExtent;

		// 开关关闭时从不剪裁
		bool culled = false;
		if (s_FrustumCullingEnabled)
			culled = !s_Data.CameraFrustum.IntersectsAABB(wMin, wMax);

		// 如果开了可视化，绿/红都记录下来供后面画线框
		if (s_DebugCullingViz)
			s_Data.DebugAABBs.push_back({ wMin, wMax, culled });

		return culled;
	}

	// ----------
	// 公开接口：推入队列
	// ----------
	static void SubmitMesh(const Ref<Mesh>& mesh, const glm::mat4& transform,
		const Ref<Material>& material, int entityID)
	{
		bool culled = CullMesh(*mesh, transform);
		if (culled)
			s_Data.Stats.CulledCount++;
		s_Data.DrawCommands.push_back({ mesh, transform, material, entityID, culled });
	}

	void Renderer3D::DrawMesh(const Ref<Mesh>& mesh, const glm::mat4& transform, const glm::vec4& color, int entityID)
	{
		if (!mesh)
			return;
		SubmitMesh(mesh, transform, Material::Create(color), entityID);
	}

	void Renderer3D::DrawMesh(const Ref<Mesh>& mesh, const glm::mat4& transform, const Ref<Texture2D>& texture, int entityID)
	{
		if (!mesh)
			return;
		SubmitMesh(mesh, transform, Material::Create(texture), entityID);
	}

	void Renderer3D::DrawMesh(const Ref<Mesh>& mesh, const glm::mat4& transform, const Ref<Material>& material, int entityID)
	{
		if (!mesh)
			return;
		SubmitMesh(mesh, transform, material ? material : s_Data.DefaultMaterial, entityID);
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
			SubmitMesh(subMesh.MeshData, finalTransform, mat, entityID);
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
			SubmitMesh(subMesh.MeshData, finalTransform, mat, entityID);
		}
	}
}
