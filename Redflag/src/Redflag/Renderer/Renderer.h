#pragma once

#include "RenderCommand.h"

#include"OrthographicCamera.h"
#include"Shader.h"

//Renderer：管理整个场景（决定画什么）
namespace Redflag {
	
	class Renderer
	{
	public:
		static void BeginScene(OrthographicCamera& camera);
		static void EndScene();

		//提交给rendercommand
		static void Submit(const Ref<Shader>& shader,const Ref<VertexArray>& vertexArray, const glm::mat4& transform = glm::mat4(1.0f));

		inline static RendererAPI::API GetAPI() { return RendererAPI::GetAPI(); }
	private:
		struct SceneData
		{
			glm::mat4 ViewProjectionMatrix;
		};

		static SceneData* m_SceneData;
	};
}