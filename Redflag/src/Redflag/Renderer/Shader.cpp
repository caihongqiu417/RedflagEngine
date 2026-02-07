#include "rfpch.h"
#include"Shader.h"

#include"Renderer.h"
#include"Platform/OpenGL/OpenGLShader.h"

namespace Redflag{

	Shader* Shader::Create(const std::string& filepath)
	{
		switch (Renderer::GetAPI())
		{
			case RendererAPI::API::None: RF_CORE_ASSERT(false, "RendererAPI::None is currently not supported!!");
			case RendererAPI::API::OpenGL: return new OpenGLShader(filepath);
		}
		RF_CORE_ASSERT(false, "Unknown RendererAPI!");
		return nullptr;
	}


	Shader* Shader::Create(const std::string& vertexSrc, const std::string& fragmentSrc)
	{
		switch (Renderer::GetAPI())
		{
			case RendererAPI::API::None: RF_CORE_ASSERT(false, "RendererAPI::None is currently not supported!!");
			case RendererAPI::API::OpenGL: return new OpenGLShader(vertexSrc, fragmentSrc);
		}
		RF_CORE_ASSERT(false, "Unknown RendererAPI!");
		return nullptr;
	}
}