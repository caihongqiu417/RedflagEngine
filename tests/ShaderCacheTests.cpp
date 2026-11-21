#include "rfpch.h"
#include "Redflag/Core/Log.h"
#include "Redflag/Renderer/Shader.h"
#include "Redflag/Renderer/VertexArray.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>

namespace fs = std::filesystem;

static const std::string s_Vertex = R"(#version 450 core
layout(location = 0) in vec2 a_Position;
void main() { gl_Position = vec4(a_Position, 0.0, 1.0); }
)";

static std::string Fragment(const std::string& color)
{
    return "#version 450 core\nlayout(location = 0) out vec4 o_Color;\n"
        "void main() { o_Color = vec4(" + color + ", 1.0); }\n";
}

static void WriteShader(const fs::path& path, const std::string& color)
{
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << "#type vertex\n" << s_Vertex << "#type fragment\n" << Fragment(color);
}

static auto CacheFiles()
{
    std::map<fs::path, fs::file_time_type> files;
    for (const auto& entry : fs::directory_iterator("assets/cache/shader/opengl"))
        files.emplace(entry.path(), entry.last_write_time());
    return files;
}

static void CheckColor(const Redflag::Ref<Redflag::Shader>& shader, int r, int g, int b)
{
    shader->Bind();
    glDrawArrays(GL_TRIANGLES, 0, 3);
    unsigned char pixel[4]{};
    glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    assert(pixel[0] == r && pixel[1] == g && pixel[2] == b && pixel[3] == 255);
    assert(glGetError() == GL_NO_ERROR);
}

int main()
{
    Redflag::Log::Init();
    assert(glfwInit());
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window = glfwCreateWindow(16, 16, "ShaderCacheTests", nullptr, nullptr);
    assert(window);
    glfwMakeContextCurrent(window);
    assert(gladLoadGLLoader((GLADloadproc)glfwGetProcAddress));

    // VAO 在最后一个共享引用释放时销毁，此时上下文仍有效。
    {
        auto vertexArray = Redflag::VertexArray::Create();
        vertexArray->Bind();
        GLint id = 0;
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &id);
        assert(id != 0 && glIsVertexArray(static_cast<GLuint>(id)) == GL_TRUE);
        auto shared = vertexArray;
        vertexArray.reset();
        assert(glIsVertexArray(static_cast<GLuint>(id)) == GL_TRUE);
        shared.reset();
        assert(glIsVertexArray(static_cast<GLuint>(id)) == GL_FALSE);
        assert(glGetError() == GL_NO_ERROR);
    }

    const auto root = fs::current_path();
    const auto run = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto testDir = root / "bin-int" / "Tests" / ("ShaderCache-" + std::to_string(run));
    fs::create_directories(testDir);
    fs::current_path(testDir);

    GLuint vao, vbo, color, framebuffer;
    const float vertices[] = { -1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f };
    glCreateVertexArrays(1, &vao);
    glCreateBuffers(1, &vbo);
    glNamedBufferData(vbo, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glVertexArrayVertexBuffer(vao, 0, vbo, 0, 2 * sizeof(float));
    glEnableVertexArrayAttrib(vao, 0);
    glVertexArrayAttribFormat(vao, 0, 2, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayAttribBinding(vao, 0, 0);
    glBindVertexArray(vao);
    glCreateTextures(GL_TEXTURE_2D, 1, &color);
    glTextureStorage2D(color, 1, GL_RGBA8, 16, 16);
    glCreateFramebuffers(1, &framebuffer);
    glNamedFramebufferTexture(framebuffer, GL_COLOR_ATTACHMENT0, color, 0);
    assert(glCheckNamedFramebufferStatus(framebuffer, GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glViewport(0, 0, 16, 16);

    {
        // 源码构造也应创建缓存目录，截断的缓存应能重新生成。
        auto yellow = Redflag::Shader::Create("inline", s_Vertex, Fragment("1.0, 1.0, 0.0"));
        CheckColor(yellow, 255, 255, 0);
        for (const auto& [path, time] : CacheFiles())
        {
            std::ofstream corrupt(path, std::ios::binary | std::ios::trunc);
            corrupt << "bad";
        }
        auto recovered = Redflag::Shader::Create("inline", s_Vertex, Fragment("1.0, 1.0, 0.0"));
        CheckColor(recovered, 255, 255, 0);

        WriteShader("first/shared.glsl", "1.0, 0.0, 0.0");
        WriteShader("second/shared.glsl", "0.0, 1.0, 0.0");
        const auto sourceTime = fs::file_time_type::clock::now() - std::chrono::hours(24);
        fs::last_write_time("first/shared.glsl", sourceTime);
        fs::last_write_time("second/shared.glsl", sourceTime);
        auto red = Redflag::Shader::Create("first/shared.glsl");
        CheckColor(red, 255, 0, 0);

        // 热加载复用缓存，两个目录下的同名文件不串用内容。
        const auto warmFiles = CacheFiles();
        auto warm = Redflag::Shader::Create("first/shared.glsl");
        CheckColor(warm, 255, 0, 0);
        assert(CacheFiles() == warmFiles);
        auto green = Redflag::Shader::Create("second/shared.glsl");
        CheckColor(green, 0, 255, 0);

        // 保留原时间戳仍按新源码编译；仅修改时间戳不重写缓存。
        WriteShader("first/shared.glsl", "0.0, 0.0, 1.0");
        fs::last_write_time("first/shared.glsl", sourceTime);
        auto blue = Redflag::Shader::Create("first/shared.glsl");
        CheckColor(blue, 0, 0, 255);
        const auto blueFiles = CacheFiles();
        fs::last_write_time("first/shared.glsl", fs::file_time_type::clock::now() + std::chrono::hours(24));
        auto touched = Redflag::Shader::Create("first/shared.glsl");
        CheckColor(touched, 0, 0, 255);
        assert(CacheFiles() == blueFiles);

        // 旧示例按名称设置矩阵与颜色。直接绘制可以同时发现链接失败和参数名丢失。
        auto flat = Redflag::Shader::Create((root / "RedflagForge/assets/shaders/FlatColor.glsl").string());
        flat->Bind();
        flat->SetMat4("u_Transform", glm::mat4(1.0f));
        flat->SetMat4("u_ViewProjection", glm::mat4(1.0f));
        flat->SetFloat4("u_Color", glm::vec4(1.0f, 0.0f, 1.0f, 1.0f));
        CheckColor(flat, 255, 0, 255);

        // 版本指令前允许空行和注释，普通 uniform 仍能按名称上传。
        auto named = Redflag::Shader::Create("named", R"(
/* legacy */
#version 330 core
layout(location = 0) in vec2 a_Position;
void main() { gl_Position = vec4(a_Position, 0.0, 1.0); }
)", R"(// A comment may contain #version 450.
#version    330 core
out vec4 o_Color;
uniform vec4 u_Tint;
void main() { o_Color = u_Tint; }
)");
        named->Bind();
        named->SetFloat4("u_Tint", glm::vec4(0, 1, 1, 1));
        CheckColor(named, 0, 255, 255);

        Redflag::ShaderLibrary library;
        library.Add("red", red);
        assert(library.Get("red") == red);
        bool missing = false;
        try { library.Get("missing"); }
        catch (const std::out_of_range&) { missing = true; }
        assert(missing && !library.Exists("missing"));
    }

    glDeleteFramebuffers(1, &framebuffer);
    glDeleteTextures(1, &color);
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
    assert(glGetError() == GL_NO_ERROR);
    glfwDestroyWindow(window);
    glfwTerminate();
    fs::current_path(root);
    assert(testDir.parent_path() == root / "bin-int" / "Tests");
    fs::remove_all(testDir);
    std::cout << "Shader GPU tests passed: VAO lifetime, content cache, warm reuse, collisions, timestamps, recovery and library lookup\n";
}
