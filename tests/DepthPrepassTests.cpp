#include "rfpch.h"
#include "Redflag/Core/Log.h"
#include "Redflag/Renderer/Framebuffer.h"
#include "Redflag/Renderer/RenderCommand.h"
#include "Redflag/Renderer/Renderer3D.h"
#include "Redflag/Renderer/UniformBuffer.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <array>
#include <cassert>
#include <iostream>
#include "RenderDocCapture.h"

using namespace Redflag;

static Ref<Texture2D> MakeTexture(const std::array<unsigned char, 4>& values, bool alpha)
{
    auto texture = Texture2D::Create(4, 1);
    std::array<unsigned char, 16> pixels;
    pixels.fill(255);
    for (int i = 0; i < 4; ++i)
        pixels[i * 4 + (alpha ? 3 : 0)] = values[i];
    texture->SetData(pixels.data(), static_cast<uint32_t>(pixels.size()));
    glTextureParameteri(texture->GetRendererID(), GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(texture->GetRendererID(), GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return texture;
}

int main(int argc, char** argv)
{
    if (argc != 1 && (argc != 3 || std::string(argv[1]) != "--capture"))
    {
        std::cerr << "Usage: DepthPrepassTests [--capture path/to/renderdoc.dll]\n";
        return 1;
    }
    RenderDocCapture capture(argc == 3 ? argv[2] : nullptr);
    Log::Init();
    assert(glfwInit());
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window = glfwCreateWindow(32, 32, "DepthPrepassTests", nullptr, nullptr);
    assert(window);
    glfwMakeContextCurrent(window);
    assert(gladLoadGLLoader((GLADloadproc)glfwGetProcAddress));
    std::cout << "GPU: " << glGetString(GL_RENDERER) << "\nVendor: " << glGetString(GL_VENDOR)
              << "\nOpenGL: " << glGetString(GL_VERSION) << "\nTest target: 32x32\n";
    RenderCommand::Init();
    Renderer3D::Init();
    Renderer3D::GetFrustumCullingEnabled() = false;
    {
        static constexpr int size = 32;
        FramebufferSpecification spec;
        spec.Width = spec.Height = size;
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
        auto base = MakeTexture({255, 64, 255, 255}, true);
        auto alpha = MakeTexture({255, 255, 64, 255}, false);
        auto white = MakeTexture({255, 255, 255, 255}, false);
        auto black = Texture2D::Create(1, 1);
        uint32_t zero = 0;
        black->SetData(&zero, sizeof(zero));

        // 只测轮廓：无场景灯光、无 IBL、SSAO 为 1，RSM 为 0。
        std::array<unsigned char, 16 + 17 * 64> lightData{};
        auto lights = UniformBuffer::Create(static_cast<uint32_t>(lightData.size()), 3);
        lights->SetData(lightData.data(), static_cast<uint32_t>(lightData.size()));
        white->Bind(12);
        black->Bind(17);

        auto clear = [&]() {
            target->Bind();
            glDepthMask(GL_TRUE);
            glDepthFunc(GL_LEQUAL);
            glDisable(GL_BLEND);
            glDisable(GL_CULL_FACE);
            glClear(GL_DEPTH_BUFFER_BIT);
            target->ClearAttachment(1, -1);
        };
        auto check = [&](bool useBase, bool useAlpha, float factor, AlphaMode mode,
                         const std::array<bool, 4>& expected, bool captureCase = false) {
            auto material = Material::Create(glm::vec4(1, 1, 1, factor));
            material->Alpha = mode;
            material->AlphaCutoff = 0.5f;
            material->BaseColorMap = useBase ? base : nullptr;
            material->AlphaMap = useAlpha ? alpha : nullptr;
            if (captureCase) capture.Begin();
            clear();
            Renderer3D::BeginScene(glm::mat4(1), glm::vec3(0, 0, 2), glm::mat4(1));
            Renderer3D::OverrideIBLFlag(false);
            Renderer3D::SetShadowData(glm::mat4(1), glm::vec4(0));
            Renderer3D::DrawMesh(mesh, glm::mat4(1), material, 42);
            glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "Depth prepass: alpha coverage");
            Renderer3D::ExecuteDepthPrepass(prepass);
            glPopDebugGroup();
            std::array<float, size * size> depths{};
            glReadPixels(0, 0, size, size, GL_DEPTH_COMPONENT, GL_FLOAT, depths.data());

            // 独立重画主 Pass，比较所有像素；不让预传递的深度掩盖主 Pass 的差异。
            clear();
            glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "Main pass: independent alpha coverage");
            Renderer3D::EndScene();
            glPopDebugGroup();
            std::array<int, size * size> ids{};
            glReadBuffer(GL_COLOR_ATTACHMENT1);
            glReadPixels(0, 0, size, size, GL_RED_INTEGER, GL_INT, ids.data());
            for (int y = 0; y < size; ++y)
                for (int x = 0; x < size; ++x)
                {
                    int pixel = y * size + x;
                    bool visible = expected[x / (size / 4)];
                    assert((depths[pixel] < 1.0f) == visible);
                    assert((ids[pixel] == 42) == visible);
                }
            assert(glGetError() == GL_NO_ERROR);
            if (captureCase) capture.End();
        };
        check(true, false, 1.0f, AlphaMode::Mask, {true, false, true, true});
        check(false, true, 1.0f, AlphaMode::Mask, {true, true, false, true});
        check(true, true, 0.75f, AlphaMode::Mask, {true, false, false, true}, true);
        check(false, false, 0.25f, AlphaMode::Mask, {false, false, false, false});
        check(true, true, 0.25f, AlphaMode::Opaque, {true, true, true, true});
        // 同一队列和 UBO 重用后，关闭两张贴图也不应残留上次 Mask 设置。
        check(false, false, 1.0f, AlphaMode::Mask, {true, true, true, true});
        // 同一个四边形单独画一次，再和右侧邻居合批。左侧像素应得到相同的间接光。
        auto opaque = Material::Create(glm::vec4(0.5f, 0.25f, 0.75f, 1.0f));
        auto red = Texture2D::Create(1, 1);
        std::array<unsigned char, 4> redPixel = {255, 0, 0, 255};
        red->SetData(redPixel.data(), sizeof(redPixel));
        glm::mat4 left(1.0f);
        left[0][0] = 0.4f;
        left[3][0] = -0.5f;
        glm::mat4 right = left;
        right[3][0] = 0.5f;

        auto drawPair = [&](bool instanced, const Ref<Texture2D>& indirect) {
            clear();
            indirect->Bind(17);
            Renderer3D::BeginScene(glm::mat4(1), glm::vec3(0, 0, 2), glm::mat4(1));
            Renderer3D::OverrideIBLFlag(false);
            Renderer3D::SetShadowData(glm::mat4(1), glm::vec4(0));
            Renderer3D::DrawMesh(mesh, left, opaque, 71);
            if (instanced)
                Renderer3D::DrawMesh(mesh, right, opaque, 72);
            Renderer3D::EndScene();
            assert(Renderer3D::GetStats().DrawCalls == 1);
            assert(target->ReadPixel(1, 8, 16) == 71);
            assert(target->ReadPixel(1, 24, 16) == (instanced ? 72 : -1));
            glm::vec4 color;
            glReadBuffer(GL_COLOR_ATTACHMENT0);
            glReadPixels(8, 16, 1, 1, GL_RGBA, GL_FLOAT, &color.x);
            return color;
        };
        const auto unlit = drawPair(false, black);
        const auto single = drawPair(false, red);
        const auto batch = drawPair(true, red);
        for (int channel = 0; channel < 4; ++channel)
            assert(std::abs(single[channel] - batch[channel]) < 0.001f);
        assert(std::abs(single.r - unlit.r - 0.5f) < 0.001f);
        assert(single.g == unlit.g && single.b == unlit.b);
        opaque->Metallic = 1.0f;
        auto metalUnlit = drawPair(false, black);
        auto metalLit = drawPair(true, red);
        assert(glm::length(metalUnlit - metalLit) < 0.001f);
        std::cout << "Single and instanced indirect light and picking passed\n";

        Renderer3D::Shutdown();
    }
    assert(glGetError() == GL_NO_ERROR);
    glfwDestroyWindow(window);
    glfwTerminate();
    std::cout << "Depth prepass and main pass alpha coverage tests passed\n";
}
