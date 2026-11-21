#include "rfpch.h"
#include "Redflag/Renderer/PostProcess.h"
#include "Redflag/Renderer/Renderer3D.h"
#include "Redflag/Core/Log.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <cassert>
#include <iostream>

// 只验证后处理，不初始化场景统计。
void Redflag::Renderer3D::LogExternalDrawCall(DrawCallInfo::PassType, uint32_t) {}
int main()
{
    Redflag::Log::Init();
    assert(glfwInit());
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window = glfwCreateWindow(32, 32, "PostProcessTests", nullptr, nullptr);
    assert(window);
    glfwMakeContextCurrent(window);
    assert(gladLoadGLLoader((GLADloadproc)glfwGetProcAddress));
    GLuint hdr;
    glCreateTextures(GL_TEXTURE_2D, 1, &hdr);
    glTextureStorage2D(hdr, 1, GL_RGBA16F, 32, 16);
    glTextureParameteri(hdr, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(hdr, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    const float color[] = { 2.0f, 1.0f, 0.5f, 1.0f };
    glClearTexImage(hdr, 0, GL_RGBA, GL_FLOAT, color);
    {
        Redflag::PostProcess first, second;
        first.GetBloomSettings().Enabled = false;
        first.GetToneMapSettings().Exposure = 0.05f;
        second.GetBloomSettings().Enabled = false;
        second.GetToneMapSettings().Exposure = 1.0f;
        first.Render(hdr, 32, 16);
        assert(first.GetColorTextureID());
        assert(first.GetBloomTextureID() == 0);
        std::vector<unsigned char> pixels(32 * 16 * 4);
        glGetTextureImage(first.GetColorTextureID(), 0, GL_RGBA, GL_UNSIGNED_BYTE, (GLsizei)pixels.size(), pixels.data());
        assert(pixels[0] > 0 && pixels[1] > 0 && pixels[2] > 0 && pixels[3] == 255);
        second.Render(hdr, 32, 16);
        assert(second.GetColorTextureID() != first.GetColorTextureID());
        std::vector<unsigned char> bright(pixels.size());
        glGetTextureImage(second.GetColorTextureID(), 0, GL_RGBA, GL_UNSIGNED_BYTE, (GLsizei)bright.size(), bright.data());
        assert(bright[0] > pixels[0] + 50);
        first.Render(hdr, 32, 16);
        std::vector<unsigned char> repeated(pixels.size());
        glGetTextureImage(first.GetColorTextureID(), 0, GL_RGBA, GL_UNSIGNED_BYTE, (GLsizei)repeated.size(), repeated.data());
        assert(repeated == pixels); // 第二实例绘制后，第一实例的参数仍正确。
        second.GetBloomSettings().Enabled = true;
        second.Render(hdr, 32, 16);
        assert(second.GetBloomTextureID());
        first.GetBloomSettings().Enabled = true;
        first.Render(hdr, 17, 9);
        GLint w, h;
        glGetTextureLevelParameteriv(first.GetColorTextureID(), 0, GL_TEXTURE_WIDTH, &w);
        glGetTextureLevelParameteriv(first.GetColorTextureID(), 0, GL_TEXTURE_HEIGHT, &h);
        assert(w == 17 && h == 9);
        glGetTextureLevelParameteriv(second.GetColorTextureID(), 0, GL_TEXTURE_WIDTH, &w);
        assert(w == 32);
        first.Render(0, 0, 0);
        assert(first.GetColorTextureID() == 0);
        assert(glGetError() == GL_NO_ERROR);
    }
    assert(glGetError() == GL_NO_ERROR);
    glDeleteTextures(1, &hdr);
    glfwDestroyWindow(window);
    glfwTerminate();
    std::cout << "PostProcess GPU tests passed\n";
}
