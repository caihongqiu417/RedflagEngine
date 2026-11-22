#include "rfpch.h"
#include "Platform/OpenGL/OpenGLShader.h"

#include <fstream>
#include <sstream>
#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>
#include <filesystem>

#include <shaderc/shaderc.hpp>
#include <spirv_cross/spirv_cross.hpp>
#include <spirv_cross/spirv_glsl.hpp>

#include "Redflag/Core/Timer.h"

namespace Redflag {

    namespace Utils {

        static GLenum ShaderTypeFromString(const std::string& type)
        {
            if (type == "vertex") return GL_VERTEX_SHADER;
            if (type == "fragment" || type == "pixel") return GL_FRAGMENT_SHADER;
            RF_CORE_ASSERT(false, "Unknown shader type!");
            return 0;
        }

        static shaderc_shader_kind GLShaderStageToShaderC(GLenum stage)
        {
            switch (stage)
            {
            case GL_VERTEX_SHADER:   return shaderc_glsl_vertex_shader;
            case GL_FRAGMENT_SHADER: return shaderc_glsl_fragment_shader;
            }
            RF_CORE_ASSERT(false);
            return (shaderc_shader_kind)0;
        }

        static const char* GLShaderStageToString(GLenum stage)
        {
            switch (stage)
            {
            case GL_VERTEX_SHADER:   return "GL_VERTEX_SHADER";
            case GL_FRAGMENT_SHADER: return "GL_FRAGMENT_SHADER";
            }
            RF_CORE_ASSERT(false);
            return nullptr;
        }

        // #version 前面可以写注释，也可以留空行。
        static size_t FirstDirective(const std::string& source)
        {
            size_t pos = source.find_first_not_of(" \t\r\n");
            while (pos != std::string::npos)
            {
                size_t end;
                if (source.compare(pos, 2, "//") == 0)
                    end = source.find_first_of("\r\n", pos + 2);
                else if (source.compare(pos, 2, "/*") == 0)
                {
                    end = source.find("*/", pos + 2);
                    if (end != std::string::npos) end += 2;
                }
                else break;
                pos = source.find_first_not_of(" \t\r\n", end);
            }
            return pos;
        }

        static bool UsesGLSL330(const std::string& source)
        {
            const size_t pos = FirstDirective(source);
            if (pos == std::string::npos || source[pos] != '#') return false;
            std::istringstream line(source.substr(pos + 1, source.find('\n', pos) - pos - 1));
            std::string directive;
            int version = 0;
            line >> directive >> version;
            return directive == "version" && version == 330;
        }

        static const char* GetCacheDirectory()
        {
            return "assets/cache/shader/opengl";
        }

        static std::filesystem::path GetCachePath(GLenum stage, const std::string& source)
        {
            // 源码改动后会得到新的缓存文件名。改编译选项或升级编译器时，也要更新下面的版本号，让着色器重新编译。
            const std::string key = "opengl45-performance-auto-locations-auto-bind-v1\n" + source;
            uint64_t hash = 14695981039346656037ull;
            for (unsigned char value : key)
            {
                hash ^= value;
                hash *= 1099511628211ull;
            }
            return std::filesystem::path(GetCacheDirectory()) /
                (std::to_string(hash) + "." + GLShaderStageToString(stage) + ".spv");
        }
    }

    // ---- 创建与销毁 ----
    OpenGLShader::OpenGLShader(const std::string& filepath, const std::string& define)
        : m_FilePath(filepath)
    {
        m_Name = std::filesystem::path(filepath).stem().string();
        auto sources = PreProcess(ReadFile(filepath));
        if (!define.empty())
        {
            m_Name += "_" + define;
            // 把宏放在 #version 下一行。启用实例化时源码多了这个宏，会得到单独的缓存文件。
            for (auto& [stage, source] : sources)
            {
                const size_t lineEnd = source.find('\n', Utils::FirstDirective(source));
                if (lineEnd != std::string::npos)
                    source.insert(lineEnd + 1, "#define " + define + "\n");
            }
        }

        Timer timer;
        CreateProgram(sources);
        RF_CORE_TRACE("Shader '{}' created in {} ms", m_Name, timer.ElapsedMillis());
    }

    OpenGLShader::OpenGLShader(const std::string& name, const std::string& vertexSrc, const std::string& fragmentSrc)
        : m_Name(name)
    {
        std::unordered_map<GLenum, std::string> sources;
        sources[GL_VERTEX_SHADER] = vertexSrc;
        sources[GL_FRAGMENT_SHADER] = fragmentSrc;

        CreateProgram(sources);
    }

    OpenGLShader::~OpenGLShader()
    {
        glDeleteProgram(m_RendererID);
    }

    std::string OpenGLShader::ReadFile(const std::string& filepath)
    {
        std::string result;
        std::ifstream in(filepath, std::ios::in | std::ios::binary);
        if (in)
        {
            in.seekg(0, std::ios::end);
            size_t size = in.tellg();
            if (size != -1)
            {
                result.resize(size);
                in.seekg(0, std::ios::beg);
                in.read(&result[0], size);
            }
            else
            {
                RF_CORE_ERROR("Could not read from file '{0}'", filepath);
            }
        }
        else
        {
            RF_CORE_ERROR("Could not open file '{0}'", filepath);
        }
        return result;
    }

    // ---- 拆分顶点与片元着色器 ----
    // 项目用 #type 标记每一段的开头，编译时从下一行 GLSL 读起。
    std::unordered_map<GLenum, std::string> OpenGLShader::PreProcess(const std::string& source)
    {
        std::unordered_map<GLenum, std::string> shaderSources;

        const char* typeToken = "#type";
        size_t typeTokenLength = strlen(typeToken);
        size_t pos = source.find(typeToken, 0);
        while (pos != std::string::npos)
        {
            size_t eol = source.find_first_of("\r\n", pos);
            RF_CORE_ASSERT(eol != std::string::npos, "Syntax error");
            size_t begin = pos + typeTokenLength + 1;
            std::string type = source.substr(begin, eol - begin);
            RF_CORE_ASSERT(Utils::ShaderTypeFromString(type), "Invalid shader type specified");

            size_t nextLinePos = source.find_first_not_of("\r\n", eol);
            RF_CORE_ASSERT(nextLinePos != std::string::npos, "Syntax error");
            pos = source.find(typeToken, nextLinePos);

            shaderSources[Utils::ShaderTypeFromString(type)] = (pos == std::string::npos) ? source.substr(nextLinePos) : source.substr(nextLinePos, pos - nextLinePos);
        }

        return shaderSources;
    }

    // ---- SPIR-V 缓存 ----
    OpenGLShader::ShaderBinaries OpenGLShader::LoadBinaries(const std::unordered_map<GLenum, std::string>& shaderSources)
    {
        std::filesystem::create_directories(Utils::GetCacheDirectory());

        shaderc::Compiler compiler;
        shaderc::CompileOptions options;
        options.SetTargetEnvironment(shaderc_target_env_opengl, shaderc_env_version_opengl_4_5);
        options.SetOptimizationLevel(shaderc_optimization_level_performance);

        options.SetAutoMapLocations(true);
        options.SetAutoBindUniforms(true);

        ShaderBinaries shaderData;

        for (auto&& [stage, source] : shaderSources)
        {
            const auto cachedPath = Utils::GetCachePath(stage, source);
            std::ifstream in(cachedPath, std::ios::binary | std::ios::ate);
            auto& data = shaderData[stage];
            if (in)
            {
                const std::streamsize size = in.tellg();
                // SPIR-V 每 4 个字节一组，开头有固定标记。文件读不全或标记不对，就从源码重新编译。
                if (size > 0 && size % sizeof(uint32_t) == 0)
                {
                    data.resize(static_cast<size_t>(size) / sizeof(uint32_t));
                    in.seekg(0);
                    if (in.read(reinterpret_cast<char*>(data.data()), size) && data[0] == 0x07230203)
                        continue;
                }
            }
            in.close();

            shaderc::SpvCompilationResult module = compiler.CompileGlslToSpv(source, Utils::GLShaderStageToShaderC(stage), m_FilePath.c_str(), options);
            if (module.GetCompilationStatus() != shaderc_compilation_status_success)
            {
                RF_CORE_ERROR(module.GetErrorMessage());
                RF_CORE_ASSERT(false);
            }
            data.assign(module.cbegin(), module.cend());

            std::ofstream out(cachedPath, std::ios::binary);
            out.write(reinterpret_cast<const char*>(data.data()), data.size() * sizeof(uint32_t));
        }

        for (auto&& [stage, data] : shaderData)
            Reflect(stage, data);
        return shaderData;
    }

    // ---- 匹配顶点与片元着色器的变量 ----
    void OpenGLShader::CompileBinaries(ShaderBinaries& binaries)
    {
        shaderc::Compiler compiler;
        shaderc::CompileOptions options;
        options.SetTargetEnvironment(shaderc_target_env_opengl, shaderc_env_version_opengl_4_5);

        for (auto& [stage, spirv] : binaries)
        {
            spirv_cross::CompilerGLSL glslCompiler(spirv);

            {
                spirv_cross::ShaderResources resources = glslCompiler.get_shader_resources();
                // 模型矩阵和材质分别编译时，可能碰巧同名。用 GLSL 里各自的 binding 编号取名，链接时就能分清。
                for (const auto& block : resources.uniform_buffers)
                {
                    std::string binding = std::to_string(glslCompiler.get_decoration(block.id, spv::DecorationBinding));
                    glslCompiler.set_name(block.base_type_id, "Block" + binding);
                    glslCompiler.set_name(block.id, "params" + binding);
                }

                // 顶点着色器传出的 UV 和片元着色器收到的 UV 共用一个 location。按它取名，两边就能对上。
                auto renameByLocation = [&](const spirv_cross::SmallVector<spirv_cross::Resource>& vars)
                {
                    for (const auto& r : vars)
                    {
                        if (glslCompiler.has_decoration(r.id, spv::DecorationLocation))
                        {
                            uint32_t loc = glslCompiler.get_decoration(r.id, spv::DecorationLocation);
                            glslCompiler.set_name(r.id, "v_loc_" + std::to_string(loc));
                        }
                    }
                };
                if (stage == GL_VERTEX_SHADER)   renameByLocation(resources.stage_outputs);
                if (stage == GL_FRAGMENT_SHADER) renameByLocation(resources.stage_inputs);
            }

            const std::string source = glslCompiler.compile();

            shaderc::SpvCompilationResult module = compiler.CompileGlslToSpv(source, Utils::GLShaderStageToShaderC(stage), m_FilePath.c_str(), options);
            if (module.GetCompilationStatus() != shaderc_compilation_status_success)
            {
                RF_CORE_ERROR(module.GetErrorMessage());
                RF_CORE_ASSERT(false);
            }

            spirv.assign(module.cbegin(), module.cend());
        }
    }

    // ---- 程序链接 ----
    void OpenGLShader::CreateProgram(const std::unordered_map<GLenum, std::string>& sources)
    {
        // GLSL 330 示例按名字设置矩阵和颜色，直接让 OpenGL 编译源码，保留这些名字。
        const bool useSource = Utils::UsesGLSL330(sources.at(GL_VERTEX_SHADER));
        ShaderBinaries binaries;
        if (!useSource)
        {
            binaries = LoadBinaries(sources);
            CompileBinaries(binaries);
        }

        GLuint program = glCreateProgram();
        std::vector<GLuint> shaderIDs;
        for (const auto& [stage, source] : sources)
        {
            GLuint shaderID = glCreateShader(stage);
            shaderIDs.push_back(shaderID);
            if (useSource)
            {
                const char* glsl = source.c_str();
                glShaderSource(shaderID, 1, &glsl, nullptr);
                glCompileShader(shaderID);

                GLint compiled;
                glGetShaderiv(shaderID, GL_COMPILE_STATUS, &compiled);
                if (!compiled)
                {
                    GLint length;
                    glGetShaderiv(shaderID, GL_INFO_LOG_LENGTH, &length);
                    std::string message(length, '\0');
                    glGetShaderInfoLog(shaderID, length, nullptr, message.data());
                    RF_CORE_ERROR("{0} ({1}): {2}", m_FilePath.empty() ? m_Name : m_FilePath,
                        Utils::GLShaderStageToString(stage), message.c_str());
                }
            }
            else
            {
                const auto& spirv = binaries.at(stage);
                glShaderBinary(1, &shaderID, GL_SHADER_BINARY_FORMAT_SPIR_V, spirv.data(),
                    static_cast<GLsizei>(spirv.size() * sizeof(uint32_t)));
                glSpecializeShader(shaderID, "main", 0, nullptr, nullptr);
            }
            glAttachShader(program, shaderID);
        }

        glLinkProgram(program);

        GLint isLinked = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &isLinked);
        if (isLinked == GL_FALSE)
        {
            GLint maxLength = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &maxLength);

            std::vector<GLchar> infoLog(maxLength);
            glGetProgramInfoLog(program, maxLength, &maxLength, &infoLog[0]);

            glDeleteProgram(program);

            for (auto id : shaderIDs)
                glDeleteShader(id);

            RF_CORE_ERROR("{0}", infoLog.data());
            RF_CORE_ASSERT(false, "Shader link failure!");
            return;
        }

        for (auto id : shaderIDs)
        {
            glDetachShader(program, id);
            glDeleteShader(id);
        }

        m_RendererID = program;
    }

    void OpenGLShader::Reflect(GLenum stage, const std::vector<uint32_t>& shaderData)
    {
        spirv_cross::Compiler compiler(shaderData);
        spirv_cross::ShaderResources resources = compiler.get_shader_resources();

        RF_CORE_TRACE("OpenGLShader::Reflect - {0} {1}", Utils::GLShaderStageToString(stage), m_FilePath);
        RF_CORE_TRACE("    {0} uniform buffers", resources.uniform_buffers.size());
        RF_CORE_TRACE("    {0} resources", resources.sampled_images.size());

        if (!resources.uniform_buffers.empty())
        {
            RF_CORE_TRACE("Uniform Buffers:");
            for (const auto& resource : resources.uniform_buffers)
            {
                const auto& bufferType = compiler.get_type(resource.base_type_id);
                uint32_t bufferSize = (uint32_t)compiler.get_declared_struct_size(bufferType);
                uint32_t binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
                int memberCount = (int)bufferType.member_types.size();

                RF_CORE_TRACE("  {0}", resource.name);
                RF_CORE_TRACE("    Size = {0}, Binding = {1}, Members = {2}", bufferSize, binding, memberCount);
            }
        }
    }

    // ---- 选择程序与设置参数 ----
    void OpenGLShader::Bind() const
    {
        glUseProgram(m_RendererID);
    }

    void OpenGLShader::Unbind() const
    {
        glUseProgram(0);
    }

    void OpenGLShader::SetInt(const std::string& name, int value)
    {
        UploadUniformInt(name, value);
    }

    void OpenGLShader::SetIntArray(const std::string& name, int* values, uint32_t count)
    {
        UploadUniformIntArray(name, values, count);
    }

    void OpenGLShader::SetFloat(const std::string& name, float value)
    {
        UploadUniformFloat(name, value);
    }

    void OpenGLShader::SetFloat3(const std::string& name, const glm::vec3& value)
    {
        UploadUniformFloat3(name, value);
    }

    void OpenGLShader::SetFloat4(const std::string& name, const glm::vec4& value)
    {
        UploadUniformFloat4(name, value);
    }

    void OpenGLShader::SetMat4(const std::string& name, const glm::mat4& value)
    {
        UploadUniformMat4(name, value);
    }

    void OpenGLShader::UploadUniformInt(const std::string& name, int value)
    {
        GLint location = glGetUniformLocation(m_RendererID, name.c_str());
        glUniform1i(location, value);
    }

    void OpenGLShader::UploadUniformIntArray(const std::string& name, int* values, uint32_t count)
    {
        GLint location = glGetUniformLocation(m_RendererID, name.c_str());
        glUniform1iv(location, count, values);
    }

    void OpenGLShader::UploadUniformFloat(const std::string& name, float value)
    {
        GLint location = glGetUniformLocation(m_RendererID, name.c_str());
        glUniform1f(location, value);
    }

    void OpenGLShader::UploadUniformFloat2(const std::string& name, const glm::vec2& value)
    {
        GLint location = glGetUniformLocation(m_RendererID, name.c_str());
        glUniform2f(location, value.x, value.y);
    }

    void OpenGLShader::UploadUniformFloat3(const std::string& name, const glm::vec3& value)
    {
        GLint location = glGetUniformLocation(m_RendererID, name.c_str());
        glUniform3f(location, value.x, value.y, value.z);
    }

    void OpenGLShader::UploadUniformFloat4(const std::string& name, const glm::vec4& value)
    {
        GLint location = glGetUniformLocation(m_RendererID, name.c_str());
        glUniform4f(location, value.x, value.y, value.z, value.w);
    }

    void OpenGLShader::UploadUniformMat3(const std::string& name, const glm::mat3& value)
    {
        GLint location = glGetUniformLocation(m_RendererID, name.c_str());
        glUniformMatrix3fv(location, 1, GL_FALSE, glm::value_ptr(value));
    }

    void OpenGLShader::UploadUniformMat4(const std::string& name, const glm::mat4& value)
    {
        GLint location = glGetUniformLocation(m_RendererID, name.c_str());
        glUniformMatrix4fv(location, 1, GL_FALSE /* GLM 的矩阵已按列存放 */, glm::value_ptr(value));
    }
}
