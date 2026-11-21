#pragma once

#include "Redflag/Renderer/Shader.h"
#include <glm/glm.hpp>
#include <vector>
#include <string>

// 这里仅用 GLenum 声明接口，glad 留在实现文件里。
typedef unsigned int GLenum;

namespace Redflag {

    /* 从 GLSL 源码创建绘制程序。对象销毁时一并释放 OpenGL 程序。 */
    class OpenGLShader : public Shader
    {
    public:
        // ---- 创建与销毁 ----
        OpenGLShader(const std::string& filepath, const std::string& define);
        OpenGLShader(const std::string& name, const std::string& vertexSrc, const std::string& fragmentSrc);
        virtual ~OpenGLShader();

        // ---- 程序与参数 ----
        // 先 Bind，再用 Set 设置颜色或矩阵。OpenGL 会把这些值写给当前选中的程序。
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

        // ---- 按名称上传参数 ----
        void UploadUniformInt(const std::string& name, int value);
        void UploadUniformIntArray(const std::string& name, int* values, uint32_t count);
        void UploadUniformFloat(const std::string& name, float value);
        void UploadUniformFloat2(const std::string& name, const glm::vec2& value);
        void UploadUniformFloat3(const std::string& name, const glm::vec3& value);
        void UploadUniformFloat4(const std::string& name, const glm::vec4& value);
        void UploadUniformMat3(const std::string& name, const glm::mat3& value);
        void UploadUniformMat4(const std::string& name, const glm::mat4& value);

    private:
        // ---- 编译与链接 ----
        std::string ReadFile(const std::string& filepath);
        std::unordered_map<GLenum, std::string> PreProcess(const std::string& source);

        using ShaderBinaries = std::unordered_map<GLenum, std::vector<uint32_t>>;
        ShaderBinaries LoadBinaries(const std::unordered_map<GLenum, std::string>& sources);
        void CompileBinaries(ShaderBinaries& binaries);
        void CreateProgram(const std::unordered_map<GLenum, std::string>& sources);
        void Reflect(GLenum stage, const std::vector<uint32_t>& shaderData);

    private:
        uint32_t m_RendererID = 0;
        std::string m_FilePath;
        std::string m_Name;

        // 从着色器读到的资源信息只写入日志，这两张表目前为空。
        std::unordered_map<std::string, ShaderUniformBuffer> m_ShaderUniformBuffers;
        std::unordered_map<std::string, ShaderResourceDeclaration> m_Resources;
    };
}