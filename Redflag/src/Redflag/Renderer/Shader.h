#pragma once

#include<string>
#include<glm/glm.hpp>
#include"Redflag/Core/Core.h"

namespace Redflag
{
	// [新增] 1. 描述单个变量 (如 float u_Roughness)
	struct ShaderUniform
	{
		std::string Name;
		// ShaderDataType Type; // 可选，暂时不用
		uint32_t Size;
		uint32_t Offset;
	};

	// [新增] 2. 描述资源 (如 sampler2D u_Texture)
	struct ShaderResourceDeclaration
	{
		std::string Name;
		uint32_t Register; // binding = 0
		uint32_t Count;
	};

	// [新增] 3. 描述 Uniform Block (如 layout(std140) uniform Camera { ... })
	struct ShaderUniformBuffer
	{
		std::string Name;
		uint32_t Index;
		uint32_t Size;
		std::unordered_map<std::string, ShaderUniform> Uniforms;
	};

	class Shader
	{
	public:
		virtual ~Shader() = default;

		virtual void Bind() const = 0;
		virtual void Unbind() const = 0;

		virtual void SetInt(const std::string& name, int value) = 0;
		virtual void SetIntArray(const std::string& name, int* values, uint32_t count) = 0;
		virtual void SetFloat(const std::string& name, float value) = 0;
		virtual void SetFloat3(const std::string& name, const glm::vec3& value) = 0;
		virtual void SetFloat4(const std::string& name, const glm::vec4& value) = 0;
		virtual void SetMat4(const std::string& name, const glm::mat4& value) = 0;

		virtual const std::string& GetName() const = 0;

		virtual const std::unordered_map<std::string, ShaderUniformBuffer>& GetShaderUniformBuffers() const = 0;
		virtual const std::unordered_map<std::string, ShaderResourceDeclaration>& GetResources() const = 0;

		static Ref<Shader> Create(const std::string& filepath);
		static Ref<Shader> Create(const std::string& name, const std::string& vertexSrc, const std::string& fragmentSrc);

	private:
		uint32_t m_RendererID;
	};


	class ShaderLibrary
	{
	public:
		void Add(const Ref<Shader>& shader);
		void Add(const std::string& name, const Ref<Shader>& shader);
		Ref<Shader> Load(const std::string& filepath);  //asset/Texture.glsl
		Ref<Shader> Load(const std::string& name, const std::string& filepath); 

		Ref<Shader> Get(const std::string& name);

		//检查是否存在指定名字的shader
		bool Exists(const std::string& name) const;
	private:
		std::unordered_map<std::string, Ref<Shader>> m_Shaders;
	};
}
