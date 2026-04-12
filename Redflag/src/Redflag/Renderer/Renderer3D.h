#pragma once

#include "Redflag/Renderer/EditorCamera.h"
#include "Redflag/Renderer/Mesh.h"
#include "Redflag/Renderer/Model.h"
#include "Redflag/Renderer/Material.h"
#include "Redflag/Renderer/Texture.h"

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
		static void BeginScene(const glm::mat4& viewProjection);

		// 在 EndScene 里统一排序并执行所有 DrawCommand
		static void EndScene();

		// 提交到队列（不会立即绘制）
		static void DrawMesh(const Ref<Mesh>& mesh, const glm::mat4& transform, const glm::vec4& color = glm::vec4(1.0f), int entityID = -1);
		static void DrawMesh(const Ref<Mesh>& mesh, const glm::mat4& transform, const Ref<Texture2D>& texture, int entityID = -1);
		static void DrawMesh(const Ref<Mesh>& mesh, const glm::mat4& transform, const Ref<Material>& material, int entityID = -1);

		// 绘制整个 Model（把所有 SubMesh 推入队列）
		static void DrawModel(const Ref<Model>& model, const glm::mat4& transform, const glm::vec4& color = glm::vec4(1.0f), int entityID = -1);

		static void ResetStats();
		static Statistics GetStats();
	};
}
