#include "rfpch.h"
#include "Redflag/Core/Log.h"
#include "Redflag/Renderer/Framebuffer.h"
#include "Redflag/Renderer/Frustum.h"
#include "Redflag/Renderer/RenderCommand.h"
#include "Redflag/Renderer/Renderer3D.h"
#include "Redflag/Renderer/UniformBuffer.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <array>
#include <cassert>
#include <iostream>
#include <glm/gtc/matrix_transform.hpp>

using namespace Redflag;

static constexpr int Size = 32;

static void Check(bool passed, const char* message)
{
    if (!passed)
    {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

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

static void Clear(const Ref<Framebuffer>& target)
{
    target->Bind();
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

static void CheckDepth(const std::array<bool, 4>& visible, const char* pass)
{
    std::array<float, Size * Size> depths;
    glReadPixels(0, 0, Size, Size, GL_DEPTH_COMPONENT, GL_FLOAT, depths.data());
    for (int y = 0; y < Size; ++y)
        for (int x = 0; x < Size; ++x)
            if (depths[y * Size + x] != (visible[x / 8] ? 0.5f : 1.0f))
            {
                std::cerr << pass << " pixel (" << x << ',' << y << ") depth=" << depths[y * Size + x]
                          << " expected=" << (visible[x / 8] ? 0.5f : 1.0f) << '\n';
                Check(false, "light-pass depth coverage");
            }
}

// 常量输入把几何关系固定下来。盘内所有样本都指向同一虚拟点，
// 这样距离衰减、世界面积和 RGB 权重可以逐项和手算结果对照。
static void CheckIndirectIntegral(const Ref<Mesh>& quad)
{
    FramebufferSpecification spec;
    spec.Width = spec.Height = 1;
    spec.Attachments = { FramebufferTextureFormat::RGBA16F };
    auto target = Framebuffer::Create(spec);
    auto shader = Shader::Create("assets/shaders/RSMIndirect.glsl");

    struct SampleData
    {
        glm::vec4 Header;
        glm::vec4 Samples[128]{};
    } samples{};
    samples.Samples[0] = glm::vec4(0, 0, 1, 0);
    auto sampleBuffer = UniformBuffer::Create(sizeof(samples), 6);
    struct ViewData
    {
        glm::mat4 InverseViewProjection{1.0f};
        glm::ivec4 Frame{0};
    } view;
    auto viewBuffer = UniformBuffer::Create(sizeof(view), 7);
    viewBuffer->SetData(&view, sizeof(view));

    // 贴图依次记录通量、发光点位置与法线、光源深度、接收点深度与法线。
    const GLuint slots[] = {13, 14, 15, 16, 18, 19};
    GLuint textures[6];
    glCreateTextures(GL_TEXTURE_2D, 6, textures);
    auto set = [&](int index, glm::vec4 value) {
        glClearTexImage(textures[index], 0, GL_RGBA, GL_FLOAT, &value.x);
    };
    for (int i = 0; i < 6; ++i)
    {
        glTextureStorage2D(textures[i], 1, GL_RGBA32F, 16, 16);
        glTextureParameteri(textures[i], GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTextureParameteri(textures[i], GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindTextureUnit(slots[i], textures[i]);
    }
    set(0, {1, 0.25f, 0, 1});
    set(2, {0.5f, 0.5f, 0, 1});
    set(3, glm::vec4(0.5f));
    set(4, glm::vec4(0.5f));
    set(5, {0, 0, 1, 1});
    Renderer3D::BeginScene(glm::mat4(1), glm::vec3(0, 0, 2), glm::mat4(1));
    Renderer3D::SetShadowData(glm::mat4(1), glm::vec4(0));
    target->Bind();
    glDisable(GL_DEPTH_TEST);
    shader->Bind();
    quad->Bind();

    auto check = [&](float radiusUV, float lightWidth, float distance) {
        samples.Header = {1, 1, radiusUV, lightWidth};
        sampleBuffer->SetData(&samples, sizeof(samples));
        set(1, {0, 0, distance, 1});
        RenderCommand::DrawIndexed(quad->GetVertexArray(), quad->GetIndexCount());
        glm::vec4 pixel;
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glReadPixels(0, 0, 1, 1, GL_RGBA, GL_FLOAT, &pixel.x);
        float radius = radiusUV * lightWidth;
        float expected = radius * radius / (3.14159265f * distance * distance);
        Check(std::abs(pixel.r - expected) < 0.001f, "RSM projected disk area and inverse-square falloff");
        Check(std::abs(pixel.g - expected * 0.25f) < 0.001f && pixel.b == 0, "RSM flux color");
    };
    check(0.2f, 4.0f, 1.0f);
    check(0.4f, 2.0f, 1.0f);
    check(0.2f, 4.0f, 2.0f);
    Renderer3D::EndScene();
    glEnable(GL_DEPTH_TEST);
    glDeleteTextures(6, textures);
    std::cout << "RSM analytic checks passed: projected disk, distance, RGB\n";
}

int main()
{
    Log::Init();
    Check(glfwInit() != 0, "GLFW init");
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    auto* window = glfwCreateWindow(Size, Size, "Renderer3DTests", nullptr, nullptr);
    Check(window != nullptr, "GL context");
    glfwMakeContextCurrent(window);
    Check(gladLoadGLLoader((GLADloadproc)glfwGetProcAddress) != 0, "GL loader");
    RenderCommand::Init();
    Renderer3D::Init();
    Renderer3D::GetFrustumCullingEnabled() = true;
    {
        FramebufferSpecification spec;
        spec.Width = spec.Height = Size;
        spec.Attachments = { FramebufferTextureFormat::DEPTH32F };
        auto shadow = Framebuffer::Create(spec);
        spec.Attachments = { FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::RGBA16F,
            FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::DEPTH32F };
        auto rsm = Framebuffer::Create(spec);
        spec.Attachments = { FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::RED_INTEGER,
            FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::DEPTH32F };
        auto mainTarget = Framebuffer::Create(spec);
        auto shadowShader = Shader::Create("assets/shaders/ShadowDepth.glsl");
        auto rsmShader = Shader::Create("assets/shaders/RSMDepth.glsl");
        auto prepassShader = Shader::Create("assets/shaders/Depth_Prepass.glsl");
        auto mesh = Mesh::Create({
            {{-1, -1, 0}, {0, 0, 1}, {0, 0}, {1, 0, 0}},
            {{ 1, -1, 0}, {0, 0, 1}, {1, 0}, {1, 0, 0}},
            {{ 1,  1, 0}, {0, 0, 1}, {1, 1}, {1, 0, 0}},
            {{-1,  1, 0}, {0, 0, 1}, {0, 1}, {1, 0, 0}} }, {0, 1, 2, 2, 3, 0});

        // 光源无亮度也应写入有效表面的位置/法线；这里只比较覆盖率。
        std::array<unsigned char, 16 + 17 * 64> lightData{};
        auto lights = UniformBuffer::Create(static_cast<uint32_t>(lightData.size()), 3);
        lights->SetData(lightData.data(), static_cast<uint32_t>(lightData.size()));
        auto material = Material::Create(glm::vec4(1, 1, 1, 0.75f));
        material->BaseColorMap = MakeTexture({255, 64, 255, 255}, true);
        material->AlphaMap = MakeTexture({255, 255, 64, 255}, false);

        // 主相机看不到 x=4 的物体，灯光仍可看到。两种材质均须保留在光源队列。
        for (AlphaMode mode : { AlphaMode::Mask, AlphaMode::Opaque, AlphaMode::Blend })
        {
            material->Alpha = mode;
            Clear(mainTarget);
            mainTarget->ClearAttachment(1, -1);
            Renderer3D::BeginScene(glm::mat4(1));
            Renderer3D::SetShadowData(glm::translate(glm::mat4(1), glm::vec3(-4, 0, 0)), glm::vec4(0));
            Renderer3D::DrawMesh(mesh, glm::translate(glm::mat4(1), glm::vec3(4, 0, 0)), material, 42);
            Check(Renderer3D::GetStats().CulledCount == 1, "camera culling count");

            const std::array<bool, 4> visible = mode == AlphaMode::Mask
                ? std::array<bool, 4>{true, false, false, true}
                : std::array<bool, 4>{mode != AlphaMode::Blend, mode != AlphaMode::Blend,
                    mode != AlphaMode::Blend, mode != AlphaMode::Blend};
            Clear(shadow);
            Renderer3D::ExecuteShadowPass(shadowShader);
            CheckDepth(visible, "Shadow");
            Clear(rsm);
            Renderer3D::ExecuteRSMPass(rsmShader);
            CheckDepth(visible, "RSM");
            std::array<float, Size * Size * 4> positions;
            glReadBuffer(GL_COLOR_ATTACHMENT1);
            glReadPixels(0, 0, Size, Size, GL_RGBA, GL_FLOAT, positions.data());
            for (int y = 0; y < Size; ++y)
                for (int x = 0; x < Size; ++x)
                    Check(positions[(y * Size + x) * 4 + 3] == (visible[x / 8] ? 1.0f : 0.0f), "RSM surface coverage");

            Clear(mainTarget);
            mainTarget->ClearAttachment(1, -1);
            Renderer3D::ExecuteDepthPrepass(prepassShader);
            Renderer3D::EndScene();
            CheckDepth({false, false, false, false}, "Main");
            Check(Renderer3D::GetStats().SubmittedCommands == 0, "main queue excludes offscreen command");
            Check(Renderer3D::GetStats().DrawCalls == (mode == AlphaMode::Blend ? 0 : 2), "light/main draw counts");
            Check(glGetError() == GL_NO_ERROR, "light/main GL state");
        }

        // 平面边界应保留，完全在外侧的盒子剔除；负缩放由世界 AABB 处理。
        auto frustum = Frustum::FromViewProjection(glm::mat4(1));
        Check(frustum.IntersectsAABB({-1, -0.5f, -0.5f}, {-1, 0.5f, 0.5f}), "frustum touching plane");
        Check(!frustum.IntersectsAABB({-2, -0.5f, -0.5f}, {-1.01f, 0.5f, 0.5f}), "frustum outside plane");
        Clear(mainTarget);
        Renderer3D::BeginScene(glm::mat4(1));
        Renderer3D::DrawMesh(mesh, glm::scale(glm::mat4(1), glm::vec3(-0.5f, 0.5f, 1)), material);
        Check(Renderer3D::GetStats().CulledCount == 0, "negative-scale bounds");
        // 同样大小的方向光纹素接收相同功率。倾斜的正面保留颜色，背面和纯金属都没有漫反射通量。
        auto reflector = Material::Create(glm::vec4(0.8f, 0.2f, 0.1f, 1));
        auto checkFlux = [&](glm::vec3 direction, float metal, float expectedWeight) {
            int count = 1;
            glm::vec4 lightDirection(direction, 0);
            glm::vec4 lightColor(1);
            std::memcpy(lightData.data(), &count, sizeof(count));
            std::memcpy(lightData.data() + 16, &lightDirection, sizeof(lightDirection));
            std::memcpy(lightData.data() + 32, &lightColor, sizeof(lightColor));
            lights->SetData(lightData.data(), static_cast<uint32_t>(lightData.size()));
            reflector->Metallic = metal;
            Renderer3D::BeginScene(glm::mat4(1));
            Renderer3D::SetShadowData(glm::mat4(1), glm::vec4(0));
            Renderer3D::DrawMesh(mesh, glm::mat4(1), reflector, 42);
            Clear(rsm);
            Renderer3D::ExecuteRSMPass(rsmShader);
            glm::vec4 flux;
            glReadBuffer(GL_COLOR_ATTACHMENT0);
            glReadPixels(16, 16, 1, 1, GL_RGBA, GL_FLOAT, &flux.x);
            const glm::vec3 expected = glm::vec3(reflector->BaseColor) * expectedWeight;
            Check(glm::length(glm::vec3(flux) - expected) < 0.001f, "RSM projected flux density");
        };
        checkFlux({0, 0, -1}, 0, 1);
        checkFlux(glm::normalize(glm::vec3(3, 0, -1)), 0, 1);
        checkFlux({0, 0, 1}, 0, 0);
        checkFlux({0, 0, -1}, 1, 0);

        CheckIndirectIntegral(mesh);
        Renderer3D::Shutdown();
    }
    Check(glGetError() == GL_NO_ERROR, "resource teardown");
    glfwDestroyWindow(window);
    glfwTerminate();
    std::cout << "Renderer3D tests passed: offscreen light casters, Mask/Opaque/Blend coverage, counts and bounds\n";
}
