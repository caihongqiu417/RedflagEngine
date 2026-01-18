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
		static void Submit(const std::shared_ptr<Shader>& shader,const std::shared_ptr<VertexArray>& vertexArray);

		inline static RendererAPI::API GetAPI() { return RendererAPI::GetAPI(); }
	private:
		struct SceneData
		{
			glm::mat4 ViewProjectionMatrix;
		};

		static SceneData* m_SceneData;
	};
}