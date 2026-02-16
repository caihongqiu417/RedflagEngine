#include "rfpch.h"
#include"Shader.h"

#include"Renderer.h"
#include"Platform/OpenGL/OpenGLShader.h"

namespace Redflag{
	
	Ref<Shader> Shader::Create(const std::string& filepath)
	{
		switch (Renderer::GetAPI())
		{
			case RendererAPI::API::None: RF_CORE_ASSERT(false, "RendererAPI::None is currently not supported!!");
			case RendererAPI::API::OpenGL: return std::make_shared<OpenGLShader>(filepath);
		}
		RF_CORE_ASSERT(false, "Unknown RendererAPI!");
		return nullptr;
	}


	Ref<Shader> Shader::Create(const std::string& name,const std::string& vertexSrc, const std::string& fragmentSrc)
	{
		switch (Renderer::GetAPI())
		{
			case RendererAPI::API::None: RF_CORE_ASSERT(false, "RendererAPI::None is currently not supported!!");
			case RendererAPI::API::OpenGL: return std::make_shared<OpenGLShader>(name, vertexSrc, fragmentSrc);
		}
		RF_CORE_ASSERT(false, "Unknown RendererAPI!");
		return nullptr;
	}

	void ShaderLibrary::Add(const Ref<Shader>& shader)
	{
		//获取shader的名字，检查是否已经存在，如果不存在就添加到unordered_map中
		auto& name = shader->GetName();
		RF_CORE_ASSERT(m_Shaders.find(name) == m_Shaders.end(), "Shader already exists!!");
		m_Shaders[name] = shader;
	}

	void ShaderLibrary::Add(const std::string& name, const Ref<Shader>& shader)
	{
		//检查是否已经存在，如果不存在就添加到unordered_map中
		RF_CORE_ASSERT(m_Shaders.find(name) == m_Shaders.end(), "Shader already exists!!");
		m_Shaders[name] = shader;
	}

	Ref<Shader> ShaderLibrary::Load(const std::string& filepath)
	{
		//根据文件路径创建shader对象，并把它添加到unordered_map中，最后返回这个shader对象
		auto shader = Shader::Create(filepath);
		Add(shader);
		return shader;
	}
	Ref<Shader> ShaderLibrary::Load(const std::string& name, const std::string& filepath)
	{
		//根据文件路径创建shader对象，并把它添加到unordered_map中，最后返回这个shader对象
		auto shader = Shader::Create(filepath);
		Add(name,shader);
		return shader;
	}
	Ref<Shader> ShaderLibrary::Get(const std::string& name)
	{
		//检查是否存在，如果存在就返回这个shader对象，否则断言失败
		RF_CORE_ASSERT(!Exists(name), "Shader not found!!");
		return m_Shaders[name];
	}

	bool ShaderLibrary::Exists(const std::string& name) const
	{
		//检查是否存在
		return m_Shaders.find(name) != m_Shaders.end();
	}
}