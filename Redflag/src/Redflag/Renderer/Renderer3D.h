#pragma once

#include "Redflag/Renderer/EditorCamera.h"
#include "Redflag/Renderer/Mesh.h"
#include "Redflag/Renderer/Model.h"
#include "Redflag/Renderer/Material.h"
#include "Redflag/Renderer/Texture.h"
#include "Redflag/Renderer/Shader.h"

#include <glm/glm.hpp>

namespace Redflag
{
	class Renderer3D
	{
	public:
			struct Statistics
			{
				uint32_t DrawCalls = 0;
				uint32_t SubmittedCommands = 0;
				uint32_t VertexCount = 0;
				uint32_t IndexCount = 0;
				uint32_t CulledCount = 0;  // 视锥体剪裁掩弃的物体/子网格数量
			};

			// 单次 DrawCall 的简略信息，供 ImGui Profile 面板展示
			struct DrawCallInfo
			{
				enum class PassType { Main, Shadow, Prepass, SSAO, BloomBright, BloomUp, BloomTonemap };
				PassType Pass       = PassType::Main;
				uint32_t IndexCount = 0;
				int      EntityID   = -1;
				int      AlphaMode  = 0;   // 0=Opaque 1=Mask 2=Blend
				uintptr_t MaterialKey = 0; // 同一 key 表示同材质
				uintptr_t MeshPtr   = 0;   // 用作区分 Mesh 的稳定 id
			};
			static const std::vector<DrawCallInfo>& GetDrawCallLog();

			// 让非 Renderer3D 的 Pass（如 SSAO）把自己的 drawcall 登记进来，
			// 便于统一在 Profile 面板 / Stats 里统计。
			static void LogExternalDrawCall(DrawCallInfo::PassType pass, uint32_t indexCount);

		static void Init();
		static void Shutdown();

		static void BeginScene(const EditorCamera& camera);
		static void BeginScene(const glm::mat4& viewProjection, const glm::vec3& cameraPosition = glm::vec3(0.0f), const glm::mat4& view = glm::mat4(1.0f));

		// 在 EndScene 里统一排序并执行所有 DrawCommand
		static void EndScene();

		// 提交到队列（不会立即绘制）
		static void DrawMesh(const Ref<Mesh>& mesh, const glm::mat4& transform, const glm::vec4& color = glm::vec4(1.0f), int entityID = -1);
		static void DrawMesh(const Ref<Mesh>& mesh, const glm::mat4& transform, const Ref<Texture2D>& texture, int entityID = -1);
		static void DrawMesh(const Ref<Mesh>& mesh, const glm::mat4& transform, const Ref<Material>& material, int entityID = -1);

		// 绘制整个 Model（把所有 SubMesh 推入队列）
		static void DrawModel(const Ref<Model>& model, const glm::mat4& transform, const glm::vec4& color = glm::vec4(1.0f), int entityID = -1);
		static void DrawModel(const Ref<Model>& model, const glm::mat4& transform, const std::vector<Ref<Material>>& overrideMaterials, const glm::vec4& fallbackColor, int entityID = -1);

		// ------------------------------------------------------------------
		// 阴影 Pass 支持
		//
		// SetShadowData：SceneRenderer 在 BeginScene 之后、EndScene 之前
		//   写入光源空间矩阵 + 阴影采样参数（bias / PCF 半径 / 开关）。
		//   主 Mesh shader 在片元阶段读取它们做阴影查询。
		//
		// ExecuteShadowPass：用指定的深度 Shader 重绘当前提交的 DrawCommands
		//   队列（仅写深度、不绑材质），用于生成 Shadow Map。调用方需要
		//   自行绑好 Shadow FBO 与视口。
		// ------------------------------------------------------------------
		static void SetShadowData(const glm::mat4& lightSpaceMatrix,
			const glm::vec4& shadowParams,
			const glm::vec4& shadowParams2 = glm::vec4(1.5f, 6.0f, 0.0f, 0.0f));
		static void ExecuteShadowPass(const Ref<Shader>& depthShader);

		// Z-Prepass：用主相机视角只写深度，不写颜色
		// 须在 BeginScene 收集完所有 DrawMesh/DrawModel 之后、EndScene 之前调用
		static void ExecuteDepthPrepass(const Ref<Shader>& depthShader);

		// 覆写 CameraUBO 中的 IBL 标志（ViewPosition.w）
		static void OverrideIBLFlag(bool enabled);

		// 视锥体剪裁开关（菜单 Toggle）
		static bool& GetFrustumCullingEnabled();

		// 剪裁可视化开关：开启后每帧会记录所有提交物体的世界空间 AABB，
		// SceneRenderer 可以调用 DrawDebugAABBs() 用 Renderer2D 画出线框。
		//   绿色 = 通过剪裁；红色 = 被剪掉。
		static bool& GetDebugCullingVizEnabled();

		// 用 Renderer2D 绘制当前帧收集到的 AABB 线框（需要先 Renderer2D::BeginScene）
		static void DrawDebugAABBs();

		static void ResetStats();
		static Statistics GetStats();
	};
}
