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
			};

		static void Init();
		static void Shutdown();

		static void BeginScene(const EditorCamera& camera);
		static void BeginScene(const glm::mat4& viewProjection, const glm::vec3& cameraPosition = glm::vec3(0.0f));

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

		static void ResetStats();
		static Statistics GetStats();
	};
}
