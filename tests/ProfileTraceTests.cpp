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
#include "DrawTiming.h"
#include "RenderDocCapture.h"

using namespace Redflag;

enum DrawPath : size_t { Individual, Instanced, DepthAndInstanced };

int main(int argc, char** argv)
{
    const bool record = argc == 4 && std::string(argv[1]) == "--capture";
    const bool measure = argc == 3 && std::string(argv[1]) == "--measure";
    const bool trace = argc == 2 && std::string(argv[1]).find("--") != 0;
    const std::string mode = record ? argv[2] : "prepass";
    if ((!trace && !record && !measure) || (mode != "separate" && mode != "instanced" && mode != "prepass"))
    {
        std::cerr << "Usage: ProfileTraceTests output-trace.json | --measure output.csv | --capture separate|instanced|prepass renderdoc.dll\n";
        return 1;
    }
    const DrawPath path = mode == "separate" ? Individual : mode == "instanced" ? Instanced : DepthAndInstanced;
    RenderDocCapture capture(record ? argv[3] : nullptr, "../output/profiling/mesh/" + mode,
        "64 opaque quads: " + mode, "256x256, 8x8 disjoint quads, ten warmup frames.");
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
              << "\nConfiguration: Debug/x64; target 256x256; 64 objects; mode: " << (measure ? "timing comparison" : mode) << '\n';

    // 初始化和预热另存，不覆盖编辑器原有的 Startup/Runtime 记录。
    if (trace)
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
        // 内容相同但各自持有网格时，引擎会逐个绘制；共享网格时引擎能自动使用实例绘制。
        std::array<Ref<Mesh>, 64> individualMeshes;
        if (path == Individual || measure)
            for (auto& entry : individualMeshes)
                entry = Mesh::Create(vertices, {0, 1, 2, 2, 3, 0});
        auto white = Texture2D::Create(1, 1);
        auto black = Texture2D::Create(1, 1);
        uint32_t whitePixel = 0xffffffff, blackPixel = 0;
        white->SetData(&whitePixel, sizeof(whitePixel));
        black->SetData(&blackPixel, sizeof(blackPixel));
        std::array<unsigned char, 16 + 17 * 64> lightData{};
        auto lights = UniformBuffer::Create(static_cast<uint32_t>(lightData.size()), 3);
        lights->SetData(lightData.data(), static_cast<uint32_t>(lightData.size()));
        white->Bind(12);
        (trace ? black : white)->Bind(17);  // 对照时用白图提供恒定间接光，让方块出现在颜色图中。

        auto drawFrame = [&](size_t drawPath) {
            RF_PROFILE_SCOPE("CPU submission - 64 opaque quads");
            target->Bind();
            glDepthMask(GL_TRUE);
            glDepthFunc(trace ? GL_LEQUAL : GL_LESS);
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
                    const int id = y * 8 + x;
                    Renderer3D::DrawMesh(drawPath == Individual ? individualMeshes[id] : mesh, transform, material, id);
                }
            if (drawPath == DepthAndInstanced)
            {
                if (record) glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "Pre-Z: depth and normals");
                Renderer3D::ExecuteDepthPrepass(prepass);
                if (record) glPopDebugGroup();
                glDepthFunc(GL_LEQUAL);
                glDepthMask(GL_FALSE);
            }
            if (record) glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "Main: color and entity IDs");
            Renderer3D::EndScene();
            if (record) glPopDebugGroup();
        };
        auto renderFrame = [&]() {
            RF_PROFILE_SCOPE("Frame");
            drawFrame(path);
            {
                RF_PROFILE_SCOPE("CPU wait - glFinish");
                glFinish();
            }
        };

        if (measure)
        {
            const std::filesystem::path csvPath = std::filesystem::absolute(argv[2]);
            std::filesystem::create_directories(csvPath.parent_path());
            MeasureDraws(csvPath, std::array<const char*, 3>{"individual", "instanced", "prez_instanced"}, drawFrame);
        }
        else
        {
            for (int frame = 0; frame < 10; ++frame)
                renderFrame();
            if (record)
            {
                capture.Begin();
                renderFrame();
                capture.End();
            }
            else
            {
                RF_PROFILE_END_SESSION();
                const std::filesystem::path tracePath = std::filesystem::absolute(argv[1]);
                std::filesystem::create_directories(tracePath.parent_path());
                RF_PROFILE_BEGIN_SESSION("Measured CPU scopes", tracePath.string());
                for (int frame = 0; frame < 60; ++frame)
                    renderFrame();
                RF_PROFILE_END_SESSION();
            }
        }

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
    std::cout << "Verified all 64 entity centers\n";
}
