#include "rfpch.h"
#include "Redflag/Renderer/PostProcess.h"
#include "Redflag/Renderer/Renderer3D.h"
#include "Redflag/Renderer/Texture.h"
#include "Redflag/Core/Log.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <cassert>
#include <iostream>
#include <filesystem>
#include <fstream>

static bool CheckTextureUpload(int channels)
{
    const auto directory = std::filesystem::path("../bin-int/Tests/TextureFixtures");
    std::filesystem::create_directories(directory);
    const auto path = directory / ("channels-" + std::to_string(channels) + ".tga");
    std::vector<unsigned char> expected(3 * 2 * channels);
    for (size_t i = 0; i < expected.size(); ++i) expected[i] = static_cast<unsigned char>(7 + i * 3);
    auto encoded = expected;
    if (channels >= 3)
        for (size_t i = 0; i < encoded.size(); i += channels) std::swap(encoded[i], encoded[i + 2]);
    {
        unsigned char header[18]{};
        header[2] = channels <= 2 ? 3 : 2;
        header[12] = 3; header[14] = 2; header[16] = channels * 8;
        header[17] = channels == 2 || channels == 4 ? 8 : 0;
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(header), sizeof(header));
        out.write(reinterpret_cast<const char*>(encoded.data()), encoded.size());
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 8);
    auto texture = Redflag::Texture2D::Create(path.string());
    const GLenum formats[] = { GL_RED, GL_RG, GL_RGB, GL_RGBA };
    std::vector<unsigned char> actual(expected.size());
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glGetTextureImage(texture->GetRendererID(), 0, formats[channels - 1], GL_UNSIGNED_BYTE,
                      static_cast<GLsizei>(actual.size()), actual.data());
    GLint alignment;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
    bool valid = actual == expected && alignment == 8;
    std::reverse(expected.begin(), expected.end());
    texture->SetData(expected.data(), static_cast<uint32_t>(expected.size()));
    glGetTextureImage(texture->GetRendererID(), 0, formats[channels - 1], GL_UNSIGNED_BYTE,
                      static_cast<GLsizei>(actual.size()), actual.data());
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
    valid &= actual == expected && alignment == 8;
    if (channels <= 2)
    {
        GLint swizzle[4];
        glGetTextureParameteriv(texture->GetRendererID(), GL_TEXTURE_SWIZZLE_RGBA, swizzle);
        valid &= swizzle[0] == GL_RED && swizzle[1] == GL_RED && swizzle[2] == GL_RED
              && swizzle[3] == (channels == 2 ? GL_GREEN : GL_ONE);
    }
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    std::cout << (valid ? "PASS " : "FAIL ") << "3x2 texture channels=" << channels
              << ": file upload, SetData and row alignment\n";
    return valid;
}

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
    for (int channels : {3, 4, 1, 2})
        if (!CheckTextureUpload(channels)) return 1;
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
        glEnable(GL_CULL_FACE);
        glCullFace(GL_FRONT_AND_BACK);
        glEnable(GL_SCISSOR_TEST);
        glScissor(0, 0, 1, 1);
        first.Render(hdr, 32, 16);
        assert(glIsEnabled(GL_CULL_FACE) && glIsEnabled(GL_SCISSOR_TEST));
        assert(first.GetColorTextureID());
        assert(first.GetBloomTextureID() == 0);
        std::vector<unsigned char> pixels(32 * 16 * 4);
        glGetTextureImage(first.GetColorTextureID(), 0, GL_RGBA, GL_UNSIGNED_BYTE, (GLsizei)pixels.size(), pixels.data());
        assert(pixels[0] > 0 && pixels[1] > 0 && pixels[2] > 0 && pixels[3] == 255);
        for (size_t i = 0; i < pixels.size(); ++i)
            assert(pixels[i] == pixels[i % 4]); // 全屏输出不继承场景剔除或 ImGui 裁剪框。
        glDisable(GL_CULL_FACE);
        glDisable(GL_SCISSOR_TEST);
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
