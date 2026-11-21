#pragma once

#include "Redflag/Renderer/Shader.h"
#include <glm/glm.hpp>
#include <vector> // SPIR-V 字数组
#include <string>

// 仅声明 GL 枚举类型，避免在公共头中引入 glad。
typedef unsigned int GLenum;

namespace Redflag {

    /*
     * 持有链接后的 OpenGL 程序，将分阶段 GLSL 编译为 SPIR-V 并交给驱动。
     * 磁盘缓存保存中间二进制；创建、绑定与销毁均依赖有效 OpenGL 上下文。
     */
    class OpenGLShader : public Shader
    {
    public:
        // ---- 创建与销毁 ----
        OpenGLShader(const std::string& filepath, const std::string& define);
        OpenGLShader(const std::string& name, const std::string& vertexSrc, const std::string& fragmentSrc);
        virtual ~OpenGLShader();

        // ---- 程序与参数 ----
        // Set 系列写入当前绑定程序的 uniform，调用前先 Bind 此对象。
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

        // ---- Uniform 上传 ----
        void UploadUniformInt(const std::string& name, int value);
        void UploadUniformIntArray(const std::string& name, int* values, uint32_t count);
        void UploadUniformFloat(const std::string& name, float value);
        void UploadUniformFloat2(const std::string& name, const glm::vec2& value);
        void UploadUniformFloat3(const std::string& name, const glm::vec3& value);
        void UploadUniformFloat4(const std::string& name, const glm::vec4& value);
        void UploadUniformMat3(const std::string& name, const glm::mat3& value);
        void UploadUniformMat4(const std::string& name, const glm::mat4& value);

    private:
        // ---- 编译阶段 ----
        std::string ReadFile(const std::string& filepath);
        std::unordered_map<GLenum, std::string> PreProcess(const std::string& source);

        using ShaderBinaries = std::unordered_map<GLenum, std::vector<uint32_t>>;
        ShaderBinaries LoadBinaries(const std::unordered_map<GLenum, std::string>& sources);
        void CompileBinaries(ShaderBinaries& binaries);
        void CreateProgram(const std::unordered_map<GLenum, std::string>& sources);
        void Reflect(GLenum stage, const std::vector<uint32_t>& shaderData);

    private:
        uint32_t m_RendererID = 0;
        std::string m_FilePath; // 源文件路径，用于定位编译错误
        std::string m_Name;

        // 当前 Reflect 仅记录日志，以下查询表尚未填充。
        std::unordered_map<std::string, ShaderUniformBuffer> m_ShaderUniformBuffers;
        std::unordered_map<std::string, ShaderResourceDeclaration> m_Resources;
    };
}