#include "rfpch.h"
#include "Renderer3D.h"
#include "Platform/OpenGL/OpenGLTransparencyPass.h"
#include "Platform/OpenGL/OpenGLTexture.h"
#include "Redflag/Renderer/Model.h"
#include "Redflag/Renderer/Material.h"

#include "Redflag/Renderer/RenderCommand.h"
#include "Redflag/Renderer/Shader.h"
#include "Redflag/Renderer/UniformBuffer.h"
#include "Redflag/Renderer/IBLPrecompute.h"
#include "Redflag/Renderer/Frustum.h"
#include "Redflag/Renderer/Renderer2D.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>

namespace Redflag
{
	// 每条记录描述场景要画的一个网格。
	struct DrawCommand
	{
		Ref<Mesh>     MeshData;
		glm::mat4     Transform;
		Ref<Material> Mat;  // 提交时已选好材质。
		int           EntityID = -1;
		bool          CameraCulled = false; // 相机看不见的物体仍可能在画面里投下影子，或把光反射进来。
		bool          Skip     = false;  // 这条记录已合批绘制。
	};

	struct Renderer3DData
	{
		Ref<Shader>        MeshShader;
		Ref<Shader>        TransparentShader;
		Scope<OpenGLTransparencyPass> Transparency;
		Ref<Material>      DefaultMaterial; // 白色纯色

		Ref<UniformBuffer> CameraUniformBuffer;
		Ref<UniformBuffer> TransformUniformBuffer;
		Ref<UniformBuffer> MaterialUniformBuffer;

		// 阴影和主画面共用同一份相机数据，字段顺序对应着色器的 Camera。
		struct CameraData
		{
			glm::mat4 ViewProjection;
			glm::vec4 ViewPosition;  // xyz 是相机世界位置，w 表示是否启用环境光贴图。
			glm::mat4 LightSpaceMatrix;  // 从世界坐标投影到灯光视角。
			glm::vec4 ShadowParams;  // 深度偏移、额外偏移、阴影图宽度的倒数、阴影模式。
			glm::vec4 ShadowParams2;  // 阴影采样半径、光源大小、主视口宽和高。
			glm::mat4 View;  // 从世界空间换到相机空间。
		};

		struct TransformData
		{
			glm::mat4 Transform;
		};

		// 前半部分和深度、阴影着色器共用；折射率和厚度只在绘制玻璃时读取。
		struct alignas(16) MaterialData
		{
			glm::vec4 BaseColor;
			float     Metallic;
			float     Roughness;
			float     AO;
			int       UseBaseColorMap;
			int       UseNormalMap;
			int       UseMetallicRoughnessMap;
			int       UseAOMap;
			int       EntityID;
			int       UseAlphaMap;
			float     AlphaCutoff;
			int       AlphaMode;
			float     RefractionIndex;
			float     Thickness;
			alignas(16) glm::vec3 Emission;
			int       UseEmissiveMap;
		};

		static_assert(sizeof(MaterialData) == 96 && offsetof(MaterialData, Emission) == 80 &&
			offsetof(MaterialData, UseEmissiveMap) == 92, "Match the std140 Material block in Renderer3D_Mesh.glsl");

		CameraData    CameraBuffer;
		TransformData TransformBuffer;
		MaterialData  MaterialBuffer;

		// 同一批物体共用网格和贴图，各自的位置、底色和实体编号存在这里。
		struct InstanceData
		{
			glm::mat4 Transform;
			glm::vec4 BaseColor;
			glm::vec4 MatParams;  // x 金属度、y 粗糙度、z 环境遮蔽。
			int       EntityID;
			glm::vec3 Emission;
		};

		static constexpr uint32_t MAX_INSTANCES = 2048;

		Ref<Shader>  InstancedMeshShader;
		uint32_t     InstanceVBO = 0;

		std::vector<DrawCommand> DrawCommands;
		std::vector<DrawCommand> TransparentCommands;

		// 当前帧视锥体（BeginScene 时构建，DrawMesh/DrawModel 使用）
		Frustum CameraFrustum;

		// 调试视图用这组角点画包围盒，Culled 决定画红框还是绿框。
		struct DebugAABB { glm::vec3 Min; glm::vec3 Max; bool Culled; };
		std::vector<DebugAABB> DebugAABBs;

		// 每帧 DrawCall 日志（供 ImGui 面板展示）
		std::vector<Renderer3D::DrawCallInfo> DrawCallLog;

		uint32_t DrawCallCount = 0;
		Renderer3D::Statistics Stats;
	};

	static Renderer3DData s_Data;

	static bool s_FrustumCullingEnabled = true;
	static bool s_DebugCullingViz = false;

	bool& Renderer3D::GetFrustumCullingEnabled()    { return s_FrustumCullingEnabled; }
	bool& Renderer3D::GetDebugCullingVizEnabled()  { return s_DebugCullingViz; }

	// ---- 材质与单次绘制 ----
	// 准备材质的颜色和贴图。拾取编号由具体绘制的物体决定。
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
		s_Data.MaterialBuffer.RefractionIndex        = mat->RefractionIndex;
		s_Data.MaterialBuffer.Thickness              = mat->Thickness;
		s_Data.MaterialBuffer.Emission               = mat->Emission;
		s_Data.MaterialBuffer.UseEmissiveMap         = mat->EmissiveMap != nullptr;

		if (mat->HasBaseColorMap())        mat->BaseColorMap->Bind(3);
		if (mat->HasNormalMap())           mat->NormalMap->Bind(4);
		if (mat->HasMetallicRoughnessMap()) mat->MetallicRoughnessMap->Bind(5);
		if (mat->HasAOMap())               mat->AOMap->Bind(6);
		if (mat->HasAlphaMap())            mat->AlphaMap->Bind(10);
		if (mat->EmissiveMap)              static_cast<OpenGLTexture2D&>(*mat->EmissiveMap).BindSRGB(13);
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
		s_Data.TransparentShader.reset();
		s_Data.Transparency.reset();
		s_Data.InstancedMeshShader.reset();
		if (s_Data.InstanceVBO) { glDeleteBuffers(1, &s_Data.InstanceVBO); s_Data.InstanceVBO = 0; }
		s_Data.DefaultMaterial.reset();
		s_Data.CameraUniformBuffer.reset();
		s_Data.TransformUniformBuffer.reset();
		s_Data.MaterialUniformBuffer.reset();
		s_Data.DrawCommands.clear();
		s_Data.TransparentCommands.clear();
	}

	// ---- 帧状态 ----
	void Renderer3D::BeginScene(const EditorCamera& camera)
	{
		s_Data.CameraBuffer.ViewProjection = camera.GetViewProjection();
		s_Data.CameraBuffer.View           = camera.GetViewMatrix();
		// 相机位置只用前三项，w 告诉着色器环境光贴图是否准备好了。
		s_Data.CameraBuffer.ViewPosition   = glm::vec4(camera.GetPosition(),
			IBLPrecompute::IsReady() ? 1.0f : 0.0f);
		// 默认关闭阴影。场景里找到投影的方向光后再填入光源矩阵和参数。
		s_Data.CameraBuffer.LightSpaceMatrix = glm::mat4(1.0f);
		s_Data.CameraBuffer.ShadowParams     = glm::vec4(0.0f);
		// 着色器用视口宽高把像素坐标换成 UV，读取该处的遮蔽和间接光。
		GLint vp[4]; glGetIntegerv(GL_VIEWPORT, vp);
		s_Data.CameraBuffer.ShadowParams2    = glm::vec4(1.5f, 6.0f, float(vp[2]), float(vp[3]));
		s_Data.CameraUniformBuffer->SetData(&s_Data.CameraBuffer, sizeof(Renderer3DData::CameraData));

		s_Data.CameraFrustum = Frustum::FromViewProjection(s_Data.CameraBuffer.ViewProjection);

		s_Data.DrawCommands.clear();
		s_Data.TransparentCommands.clear();
		s_Data.DebugAABBs.clear();
		s_Data.DrawCallLog.clear();
		ResetStats();
	}

	void Renderer3D::BeginScene(const glm::mat4& viewProjection, const glm::vec3& cameraPosition, const glm::mat4& view)
	{
		s_Data.CameraBuffer.ViewProjection = viewProjection;
		s_Data.CameraBuffer.View           = view;
		s_Data.CameraBuffer.ViewPosition   = glm::vec4(cameraPosition,
			IBLPrecompute::IsReady() ? 1.0f : 0.0f);
		s_Data.CameraBuffer.LightSpaceMatrix = glm::mat4(1.0f);
		s_Data.CameraBuffer.ShadowParams     = glm::vec4(0.0f);
		GLint vp[4]; glGetIntegerv(GL_VIEWPORT, vp);
		s_Data.CameraBuffer.ShadowParams2    = glm::vec4(1.5f, 6.0f, float(vp[2]), float(vp[3]));
		s_Data.CameraUniformBuffer->SetData(&s_Data.CameraBuffer, sizeof(Renderer3DData::CameraData));

		s_Data.CameraFrustum = Frustum::FromViewProjection(viewProjection);

		s_Data.DrawCommands.clear();
		s_Data.TransparentCommands.clear();
		s_Data.DebugAABBs.clear();
		s_Data.DrawCallLog.clear();
		ResetStats();
	}

	// ---- 主画面 ----
	static void DrawMeshes(const std::vector<DrawCommand>& commands, const Ref<Shader>& shader,
		Renderer3D::DrawCallInfo::PassType pass)
	{
		shader->Bind();

		// 这些物体都从同一组环境贴图读取光照。
		IBLPrecompute::Bind();

		// 相邻物体即使共用材质，也需要各自的拾取编号。
		uintptr_t lastKey = ~0ull;
		for (const auto& cmd : commands)
		{
			const Ref<Material>& mat = cmd.Mat;
			uintptr_t key = mat->GetSortKey();

			if (key != lastKey)
			{
				UploadMaterial(mat);
				if (mat->Alpha == AlphaMode::Blend)
				{
					if (mat->DoubleSided) glDisable(GL_CULL_FACE);
					else glEnable(GL_CULL_FACE);
				}
				lastKey = key;
			}

			s_Data.TransformBuffer.Transform = cmd.Transform;
			s_Data.TransformUniformBuffer->SetData(&s_Data.TransformBuffer, sizeof(Renderer3DData::TransformData));
			s_Data.MaterialBuffer.EntityID = cmd.EntityID;
			s_Data.MaterialUniformBuffer->SetData(&s_Data.MaterialBuffer, sizeof(Renderer3DData::MaterialData));

			cmd.MeshData->Bind();
			RenderCommand::DrawIndexed(cmd.MeshData->GetVertexArray(), cmd.MeshData->GetIndexCount());
			s_Data.DrawCallCount++;

			s_Data.DrawCallLog.push_back({ pass,
				cmd.MeshData->GetIndexCount(), cmd.EntityID,
				static_cast<int>(mat->Alpha), key,
				reinterpret_cast<uintptr_t>(cmd.MeshData.get()) });
		}
	}

	void Renderer3D::EndScene()
	{
		DrawOpaque();
		DrawTransparency();
	}

	void Renderer3D::DrawOpaque()
	{
		RF_PROFILE_FUNCTION();

		// 视野外的物体也可能把影子投进画面。先用所有物体画阴影，到这里才去掉相机看不到的物体。
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

		// 保留提交玻璃的顺序，天空填好背景后再合成这些网格。
		auto transparent = std::stable_partition(s_Data.DrawCommands.begin(), s_Data.DrawCommands.end(),
			[](const DrawCommand& command) { return command.Mat->Alpha != AlphaMode::Blend; });
		s_Data.TransparentCommands.assign(std::make_move_iterator(transparent),
			std::make_move_iterator(s_Data.DrawCommands.end()));
		s_Data.DrawCommands.erase(transparent, s_Data.DrawCommands.end());

		if (s_Data.DrawCommands.empty())
			return;

		// 两个以上的不透明物体共用网格和全部贴图时，可以一次画出。
		if (s_Data.InstancedMeshShader && s_Data.InstanceVBO)
		{
			struct Candidate
			{
				size_t CommandIndex;
				std::array<const void*, 7> Key;
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
						mat->AlphaMap.get(), mat->EmissiveMap.get() } });
				}
			}

			// 把共用网格和贴图的物体排在一起。
			std::sort(candidates.begin(), candidates.end(),
				[](const Candidate& a, const Candidate& b)
				{
					return std::lexicographical_compare(a.Key.begin(), a.Key.end(),
						b.Key.begin(), b.Key.end(), std::less<const void*>());
				});

			bool didInstance = false;

			s_Data.InstancedMeshShader->Bind();
			IBLPrecompute::Bind();

			static std::vector<Renderer3DData::InstanceData> instBuf;

			size_t grpStart = 0;
			while (grpStart < candidates.size())
			{
				size_t grpEnd = grpStart;
				while (grpEnd < candidates.size() &&
					candidates[grpEnd].Key == candidates[grpStart].Key)
					grpEnd++;

				size_t grpSize = grpEnd - grpStart;

				if (grpSize >= 2)
				{
					didInstance = true;
					const Ref<Mesh>& mesh = s_Data.DrawCommands[candidates[grpStart].CommandIndex].MeshData;

					for (size_t k = grpStart; k < grpEnd; k++)
						s_Data.DrawCommands[candidates[k].CommandIndex].Skip = true;

					// 用第一个物体选整批的贴图，每个物体自己的颜色和粗糙度稍后单独填写。
					const Ref<Material>& batchMat = s_Data.DrawCommands[candidates[grpStart].CommandIndex].Mat;
					UploadMaterial(batchMat);
					s_Data.MaterialBuffer.EntityID = 0;  // 拾取编号从每个实例单独读取。
					s_Data.MaterialUniformBuffer->SetData(&s_Data.MaterialBuffer, sizeof(Renderer3DData::MaterialData));

					// 一组物体超过实例缓冲容量时，分几次画完。
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
							instBuf[k].Emission = mat->Emission;
						}

						glNamedBufferSubData(s_Data.InstanceVBO, 0,
							(GLsizeiptr)(batchCount * sizeof(Renderer3DData::InstanceData)),
							instBuf.data());

						// 顶点随网格读取，位置矩阵、颜色和材质参数则每画一个物体换一份。
						mesh->Bind();
						glBindBuffer(GL_ARRAY_BUFFER, s_Data.InstanceVBO);
						constexpr GLsizei stride = (GLsizei)sizeof(Renderer3DData::InstanceData);

						// 着色器分四列读取一个模型矩阵。
						for (int col = 0; col < 4; col++)
						{
							glEnableVertexAttribArray(5 + col);
							glVertexAttribPointer(5 + col, 4, GL_FLOAT, GL_FALSE, stride,
								(const void*)(offsetof(Renderer3DData::InstanceData, Transform) + col * sizeof(glm::vec4)));
							glVertexAttribDivisor(5 + col, 1 /* 每画一个实例换一次矩阵 */);
						}
						glEnableVertexAttribArray(9);
						glVertexAttribPointer(9, 4, GL_FLOAT, GL_FALSE, stride,
							(const void*)offsetof(Renderer3DData::InstanceData, BaseColor));
						glVertexAttribDivisor(9, 1);
						glEnableVertexAttribArray(10);
						glVertexAttribPointer(10, 4, GL_FLOAT, GL_FALSE, stride,
							(const void*)offsetof(Renderer3DData::InstanceData, MatParams));
						glVertexAttribDivisor(10, 1);
						// 实体编号按整数传入，编辑器用它判断点中了哪一个物体。
						glEnableVertexAttribArray(11);
						glVertexAttribIPointer(11, 1, GL_INT, stride,
							(const void*)offsetof(Renderer3DData::InstanceData, EntityID));
						glVertexAttribDivisor(11, 1);
						glEnableVertexAttribArray(12);
						glVertexAttribPointer(12, 3, GL_FLOAT, GL_FALSE, stride,
							(const void*)offsetof(Renderer3DData::InstanceData, Emission));
						glVertexAttribDivisor(12, 1);

						glDrawElementsInstanced(GL_TRIANGLES,
							(GLsizei)mesh->GetIndexCount(), GL_UNSIGNED_INT,
							nullptr /* 从索引开头画 */, (GLsizei)batchCount);
						s_Data.DrawCallCount++;

						s_Data.DrawCallLog.push_back({
							Renderer3D::DrawCallInfo::PassType::Main,
							mesh->GetIndexCount(),
							s_Data.DrawCommands[candidates[grpStart + offset].CommandIndex].EntityID,
							(int)AlphaMode::Opaque, 0,
							reinterpret_cast<uintptr_t>(mesh.get()) });

						// 这个网格还会单独绘制，画完这一批就停用逐物体的输入。
						for (int loc = 5; loc <= 12; loc++)
							glDisableVertexAttribArray(loc);

						offset += batchCount;
					}
				}

				grpStart = grpEnd;
			}

			// 已经合批的物体从队列移除，避免画第二遍。
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

		// 让材质相同的物体连续绘制，中途可以沿用同一组贴图。
		std::sort(s_Data.DrawCommands.begin(), s_Data.DrawCommands.end(),
			[](const DrawCommand& a, const DrawCommand& b)
			{
				uintptr_t ka = a.Mat->GetSortKey();
				uintptr_t kb = b.Mat->GetSortKey();
				return ka < kb;
			});

		DrawMeshes(s_Data.DrawCommands, s_Data.MeshShader, DrawCallInfo::PassType::Main);
	}

	void Renderer3D::DrawTransparency()
	{
		if (s_Data.TransparentCommands.empty())
			return;

		if (!s_Data.Transparency)
		{
			s_Data.Transparency = CreateScope<OpenGLTransparencyPass>();
			s_Data.TransparentShader = Shader::Create("assets/shaders/Renderer3D_Mesh.glsl", "TRANSPARENT");
		}

		s_Data.Transparency->Begin();
		DrawMeshes(s_Data.TransparentCommands, s_Data.TransparentShader, DrawCallInfo::PassType::Transparency);
		s_Data.Transparency->Composite();
		LogExternalDrawCall(DrawCallInfo::PassType::Transparency, 3);
		s_Data.TransparentCommands.clear();
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
		// 全屏效果也计入绘制次数，日志中相应的网格和材质留空。
		s_Data.DrawCallCount++;
		s_Data.DrawCallLog.push_back({ pass, indexCount, -1, 0, 0, 0 });
	}

	// ---- 包围盒预览 ----
	// 绿色表示包围盒与主相机视野相交，红色表示在视野外。
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

	// ---- 阴影与深度 ----
	void Renderer3D::SetShadowData(const glm::mat4& lightSpaceMatrix,
		const glm::vec4& shadowParams, const glm::vec4& shadowParams2)
	{
		s_Data.CameraBuffer.LightSpaceMatrix = lightSpaceMatrix;
		s_Data.CameraBuffer.ShadowParams     = shadowParams;
		// 只改阴影软化所需的两项，后两项仍是主画面的宽高。
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

	// 从灯光方向写深度。树叶等镂空材质沿用主画面的透明裁剪，否则影子会填满整片。
	void Renderer3D::ExecuteShadowPass(const Ref<Shader>& depthShader)
	{
		RF_PROFILE_FUNCTION();

		if (!depthShader || s_Data.DrawCommands.empty())
			return;

		depthShader->Bind();

		for (const auto& cmd : s_Data.DrawCommands)
		{

			// 玻璃跳过这张阴影图。墙和镂空的树叶即使在镜头外，也可能把影子投进画面。
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
			s_Data.DrawCallCount++;

			const Ref<Material>& smat = cmd.Mat;
			s_Data.DrawCallLog.push_back({ Renderer3D::DrawCallInfo::PassType::Shadow,
				cmd.MeshData->GetIndexCount(), cmd.EntityID,
				static_cast<int>(smat->Alpha), smat->GetSortKey(),
				reinterpret_cast<uintptr_t>(cmd.MeshData.get()) });
		}
	}

	// 反射阴影图还记录灯光看到的表面位置、朝向和反射通量密度。
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

			s_Data.TransformBuffer.Transform = cmd.Transform;
			s_Data.TransformUniformBuffer->SetData(&s_Data.TransformBuffer, sizeof(Renderer3DData::TransformData));

			// 底色和金属度决定漫反射的颜色，镂空处跳过。
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

	// 从主相机先画深度和法线，供环境遮蔽和间接光使用。
	void Renderer3D::ExecuteDepthPrepass(const Ref<Shader>& depthShader)
	{
		RF_PROFILE_FUNCTION();

		if (!depthShader || s_Data.DrawCommands.empty())
			return;

		// 按物体原点离相机的远近排序，近处先写深度，后面被挡住的像素就有机会提前跳过。
		const glm::vec3 camPos = glm::vec3(s_Data.CameraBuffer.ViewPosition);
		std::sort(s_Data.DrawCommands.begin(), s_Data.DrawCommands.end(),
			[&camPos](const DrawCommand& a, const DrawCommand& b)
			{
				glm::vec3 pa = glm::vec3(a.Transform[3]);
				glm::vec3 pb = glm::vec3(b.Transform[3]);
				float da = glm::dot(pa - camPos, pa - camPos);
				float db = glm::dot(pb - camPos, pb - camPos);
				return da < db;
			});

		glColorMaski(0 /* 场景颜色 */, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
		glColorMaski(1 /* 拾取编号 */, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
		glColorMaski(2 /* 观察空间法线 */, GL_TRUE,  GL_TRUE,  GL_TRUE,  GL_TRUE);

		depthShader->Bind();

		for (const auto& cmd : s_Data.DrawCommands)
		{

			if (cmd.CameraCulled)
				continue;

			// 半透明表面不预写深度，否则其后方可透见的物体会被挡住。
			const Ref<Material>& mat = cmd.Mat;
			if (mat->Alpha == AlphaMode::Blend)
				continue;

			s_Data.TransformBuffer.Transform = cmd.Transform;
			s_Data.TransformUniformBuffer->SetData(&s_Data.TransformBuffer, sizeof(Renderer3DData::TransformData));

			// 画叶片的深度时也要按材质和贴图裁出轮廓，否则叶片之间的空隙会挡住后面的物体。
			if (mat->Alpha == AlphaMode::Mask)
			{
				UploadMaterial(mat);
				s_Data.MaterialUniformBuffer->SetData(&s_Data.MaterialBuffer, sizeof(Renderer3DData::MaterialData));
			}
			else
			{
				s_Data.MaterialBuffer.AlphaMode = 0;
				s_Data.MaterialUniformBuffer->SetData(&s_Data.MaterialBuffer, sizeof(Renderer3DData::MaterialData));
			}

			cmd.MeshData->Bind();
			RenderCommand::DrawIndexed(cmd.MeshData->GetVertexArray(), cmd.MeshData->GetIndexCount());
			s_Data.DrawCallCount++;

			s_Data.DrawCallLog.push_back({ Renderer3D::DrawCallInfo::PassType::Prepass,
				cmd.MeshData->GetIndexCount(), cmd.EntityID,
				static_cast<int>(mat->Alpha), mat->GetSortKey(),
				reinterpret_cast<uintptr_t>(cmd.MeshData.get()) });
		}

		glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glColorMaski(1, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glColorMaski(2, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	}

	// ---- 相机剔除 ----
	// 中心按模型矩阵变换。各局部轴在世界轴上的投影长度相加，得到新的包围盒半长。
	static bool CullMesh(const Mesh& mesh, const glm::mat4& transform)
	{
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

		bool culled = false;
		if (s_FrustumCullingEnabled)
			culled = !s_Data.CameraFrustum.IntersectsAABB(wMin, wMax);

		// 预览也要画出被剔除的物体，方便检查包围盒有没有算错。
		if (s_DebugCullingViz)
			s_Data.DebugAABBs.push_back({ wMin, wMax, culled });

		return culled;
	}

	// ---- 提交网格与模型 ----
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
			// 优先用场景里的替换材质，其次用模型自带的材质，两者都没有时才用纯色。
			const auto& mat = model->GetMaterial(i, overrideMaterials);
			SubmitMesh(subMesh.MeshData, finalTransform, mat ? mat : fallback, entityID);
		}
	}
}
