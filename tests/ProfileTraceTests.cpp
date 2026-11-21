#include "rfpch.h"
#include "Redflag/Core/Log.h"
#include "Redflag/Renderer/Framebuffer.h"
#include "Redflag/Renderer/RenderCommand.h"
#include "Redflag/Renderer/Renderer3D.h"
#include "Redflag/Renderer/UniformBuffer.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <cassert>
#include <filesystem>
#include <iostream>

using namespace Redflag;

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "Usage: ProfileTraceTests output-trace.json\n";
        return 1;
    }
    const std::filesystem::path tracePath = std::filesystem::absolute(argv[1]);
    std::filesystem::create_directories(tracePath.parent_path());
    Log::Init();
    assert(glfwInit());
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window = glfwCreateWindow(256, 256, "ProfileTraceTests", nullptr, nullptr);
    assert(window);
    glfwMakeContextCurrent(window);
    assert(gladLoadGLLoader((GLADloadproc)glfwGetProcAddress));
    std::cout << "GPU: " << glGetString(GL_RENDERER) << "\nOpenGL: " << glGetString(GL_VERSION)
              << "\nConfiguration: Debug/x64; target 256x256; shared mesh 64 instances\n";

    // 初始化和预热另存，不覆盖编辑器原有的 Startup/Runtime 记录。
    RF_PROFILE_BEGIN_SESSION("Profile warmup", "../bin-int/Tests/ProfileTrace-warmup.json");
    RenderCommand::Init();
    Renderer3D::Init();
    Renderer3D::GetFrustumCullingEnabled() = false;
    {
        FramebufferSpecification spec;
        spec.Width = spec.Height = 256;
        spec.Attachments = { FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::RED_INTEGER,
                             FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::DEPTH32F };
        auto target = Framebuffer::Create(spec);
        auto prepass = Shader::Create("assets/shaders/Depth_Prepass.glsl");
        const std::vector<MeshVertex> vertices = {
            {{-1, -1, 0}, {0, 0, 1}, {0, 0}, {1, 0, 0}},
            {{ 1, -1, 0}, {0, 0, 1}, {1, 0}, {1, 0, 0}},
            {{ 1,  1, 0}, {0, 0, 1}, {1, 1}, {1, 0, 0}},
            {{-1,  1, 0}, {0, 0, 1}, {0, 1}, {1, 0, 0}}
        };
        auto mesh = Mesh::Create(vertices, {0, 1, 2, 2, 3, 0});
        auto material = Material::Create(glm::vec4(1));
        auto white = Texture2D::Create(1, 1);
        auto black = Texture2D::Create(1, 1);
        uint32_t whitePixel = 0xffffffff, blackPixel = 0;
        white->SetData(&whitePixel, sizeof(whitePixel));
        black->SetData(&blackPixel, sizeof(blackPixel));
        std::array<unsigned char, 16 + 17 * 64> lightData{};
        auto lights = UniformBuffer::Create(static_cast<uint32_t>(lightData.size()), 3);
        lights->SetData(lightData.data(), static_cast<uint32_t>(lightData.size()));
        white->Bind(12);
        black->Bind(17);

        auto renderFrame = [&]() {
            RF_PROFILE_SCOPE("Frame");
            {
                RF_PROFILE_SCOPE("CPU submission - 64 opaque quads");
                target->Bind();
                glDepthMask(GL_TRUE);
                glDepthFunc(GL_LEQUAL);
                glDisable(GL_BLEND);
                glDisable(GL_CULL_FACE);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                target->ClearAttachment(1, -1);
                Renderer3D::BeginScene(glm::mat4(1), glm::vec3(0, 0, 2), glm::mat4(1));
                Renderer3D::OverrideIBLFlag(false);
                Renderer3D::SetShadowData(glm::mat4(1), glm::vec4(0));
                for (int y = 0; y < 8; ++y)
                    for (int x = 0; x < 8; ++x)
                    {
                        auto transform = glm::translate(glm::mat4(1), glm::vec3(-0.875f + x * 0.25f,
                                                                              -0.875f + y * 0.25f, 0));
                        transform = glm::scale(transform, glm::vec3(0.1f, 0.1f, 1));
                        Renderer3D::DrawMesh(mesh, transform, material, y * 8 + x);
                    }
                Renderer3D::ExecuteDepthPrepass(prepass);
                glDepthMask(GL_FALSE);
                Renderer3D::EndScene();
            }
            {
                // 这是 CPU 等待 GPU 完成的时间，不能当成 GPU 执行时间。
                RF_PROFILE_SCOPE("CPU wait - glFinish");
                glFinish();
            }
        };

        for (int frame = 0; frame < 10; ++frame)
            renderFrame();
        RF_PROFILE_END_SESSION();
        RF_PROFILE_BEGIN_SESSION("Measured CPU scopes", tracePath.string());
        for (int frame = 0; frame < 60; ++frame)
            renderFrame();
        RF_PROFILE_END_SESSION();

        // 验证实际完成了绘制；读回放在采集区间外。
        std::array<int, 256 * 256> ids{};
        glReadBuffer(GL_COLOR_ATTACHMENT1);
        glReadPixels(0, 0, 256, 256, GL_RED_INTEGER, GL_INT, ids.data());
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                assert(ids[(y * 32 + 16) * 256 + x * 32 + 16] == y * 8 + x);
        assert(glGetError() == GL_NO_ERROR);
        Renderer3D::Shutdown();
    }
    assert(glGetError() == GL_NO_ERROR);
    glfwDestroyWindow(window);
    glfwTerminate();
    std::cout << "Profile trace complete: 10 warmup frames, 60 measured frames; 64 entity centers verified\n";
}
