#include "rfpch.h"
#include"VertexArray.h"
#include"Platform/OpenGL/OpenGLVertexArray.h"

#include"Renderer.h"

namespace Redflag {

	//工厂函数，根据实际API
	Ref<VertexArray> VertexArray::Create()
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::API::None: RF_CORE_ASSERT(false, "RendererAPI::None is currently not supported!");return nullptr;
		case RendererAPI::API::OpenGL: return std::make_shared<OpenGLVertexArray>();

		}
		RF_CORE_ASSERT(false, "Unknown RendererAPI!");
		return nullptr;
	}
}