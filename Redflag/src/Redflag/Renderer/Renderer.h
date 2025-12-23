#pragma once

#include "RenderCommand.h"

//Renderer：管理整个场景（决定画什么）
namespace Redflag {
	
	class Renderer
	{
	public:
		static void BeginScene();
		static void EndScene();

		//提交给rendercommand
		static void Submit(const std::shared_ptr<VertexArray>& vertexArray);

		inline static RendererAPI::API GetAPI() { return RendererAPI::GetAPI(); }
	};
}