#pragma once

#include "Redflag/Renderer/Shader.h"
#include <glm/glm.hpp>
#include <vector> // 修复 vector 报错
#include <string>

// 解决 GLenum 报错
typedef unsigned int GLenum;

namespace Redflag {

    class OpenGLShader : public Shader
    {
    public:
        OpenGLShader(const std::string& filepath);
        OpenGLShader(const std::string& name, const std::string& vertexSrc, const std::string& fragmentSrc);
        virtual ~OpenGLShader();

        virtual void Bind() const override;
        virtual void Unbind() const override;

        virtual void SetInt(const std::string& name, int value) override;
        virtual void SetIntArray(const std::string& name, int* values, uint32_t count) override;
        virtual void SetFloat(const std::string& name, float value) override;
        virtual void SetFloat3(const std::string& name, const glm::vec3& value) override;
        virtual void SetFloat4(const std::string& name, const glm::vec4& value) override;
        virtual void SetMat4(const std::string& name, const glm::mat4& value) override;

        virtual const std::string& GetName() const override { return m_Name; }

        virtual const std::unordered_map<std::string, ShaderUniformBuffer>& GetShaderUniformBuffers() const override { return m_ShaderUniformBuffers; }
        virtual const std::unordered_map<std::string, ShaderResourceDeclaration>& GetResources() const override { return m_Resources; }

        // [新增] 声明 UploadUniform 系列函数
        void UploadUniformInt(const std::string& name, int value);
        void UploadUniformIntArray(const std::string& name, int* values, uint32_t count);
        void UploadUniformFloat(const std::string& name, float value);
        void UploadUniformFloat2(const std::string& name, const glm::vec2& value);
        void UploadUniformFloat3(const std::string& name, const glm::vec3& value);
        void UploadUniformFloat4(const std::string& name, const glm::vec4& value);
        void UploadUniformMat3(const std::string& name, const glm::mat3& value);
        void UploadUniformMat4(const std::string& name, const glm::mat4& value);

    private:
        std::string ReadFile(const std::string& filepath);
        std::unordered_map<GLenum, std::string> PreProcess(const std::string& source);

        void CompileOrGetVulkanBinaries(const std::unordered_map<GLenum, std::string>& shaderSources);
        void CompileOrGetOpenGLBinaries();
        void CreateProgram();
        void Reflect(GLenum stage, const std::vector<uint32_t>& shaderData);

    private:
        uint32_t m_RendererID;
        std::string m_FilePath; // [修复] 添加缺失的成员变量
        std::string m_Name;

        std::unordered_map<GLenum, std::vector<uint32_t>> m_VulkanSPIRV;
        std::unordered_map<GLenum, std::vector<uint32_t>> m_OpenGLSPIRV;
        std::unordered_map<GLenum, std::string> m_OpenGLSourceCode;

        std::unordered_map<std::string, ShaderUniformBuffer> m_ShaderUniformBuffers;
        std::unordered_map<std::string, ShaderResourceDeclaration> m_Resources;
    };
}