#include"rfpch.h"
#include "OpenGLRendererAPI.h"

#include<glad/glad.h>

namespace Redflag {

	void OpenGLRendererAPI::Init()
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // 透明度混合:最终颜色 = 源颜色×Alpha + 背景颜色×(1-Alpha)
	}

	void Redflag::OpenGLRendererAPI::SetClearColor(const glm::vec4& color)
	{
		glClearColor(color.r, color.g, color.b, color.a);
	}
	void OpenGLRendererAPI::Clear()
	{
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	}
	void OpenGLRendererAPI::DrawIndexed(const Redflag::Ref<VertexArray>& vertexArray)
	{
		glDrawElements(GL_TRIANGLES, vertexArray->GetIndexBuffer()->GetCount(),GL_UNSIGNED_INT,nullptr);
	}
}