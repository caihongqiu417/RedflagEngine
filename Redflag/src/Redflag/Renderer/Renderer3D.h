#pragma once

#include "Redflag/Renderer/EditorCamera.h"
#include "Redflag/Renderer/Mesh.h"
#include "Redflag/Renderer/Texture.h"

#include <glm/glm.hpp>

namespace Redflag
{
	class Renderer3D
	{
	public:
		static void Init();
		static void Shutdown();

		static void BeginScene(const EditorCamera& camera);
		static void EndScene();

		// 纯色绘制
		static void DrawMesh(const Ref<Mesh>& mesh, const glm::mat4& transform, const glm::vec4& color = glm::vec4(1.0f));

		// 纹理绘制
		static void DrawMesh(const Ref<Mesh>& mesh, const glm::mat4& transform, const Ref<Texture2D>& texture);
	};
}
