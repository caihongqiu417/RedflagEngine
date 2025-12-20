#include "rfpch.h"
#include"VertexArray.h"
#include"Platform/OpenGL/OpenGLVertexArray.h"

#include"Renderer.h"

namespace Redflag {

	//工厂函数，根据实际API
	VertexArray* VertexArray::Create()
	{
		switch (Renderer::GetAPI())
		{
			case RendererAPI::None: RF_CORE_ASSERT(false, "RendererAPI::None is currently not supported!");return nullptr;
			case RendererAPI::OpenGL: return new OpenGLVertexArray();

		}
		RF_CORE_ASSERT(false, "Unknown RendererAPI!");
		return nullptr;
	}
}