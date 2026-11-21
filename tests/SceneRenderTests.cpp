#include "rfpch.h"
#include "Redflag/Core/Log.h"
#include "Redflag/Renderer/Framebuffer.h"
#include "Redflag/Renderer/RenderCommand.h"
#include "Redflag/Renderer/Renderer2D.h"
#include "Redflag/Renderer/Renderer3D.h"
#include "Redflag/Renderer/SceneRenderer.h"
#include "Redflag/Renderer/Shader.h"
#include "Redflag/Scene/Scene.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <cmath>
#include <iostream>
#include <glm/gtc/matrix_transform.hpp>

using namespace Redflag;
static int s_Failures = 0;
static void Check(bool condition, const char* name)
{
    std::cout << (condition ? "PASS " : "FAIL ") << name << '\n';
    s_Failures += !condition;
}

static int ReadHistoryReset()
{
    GLint buffer;
    glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, 8, &buffer);
    int reset;
    glGetNamedBufferSubData(buffer, 132, sizeof(reset), &reset);
    return reset;
}

static void CheckRSMSamples(int count)
{
    GLint buffer;
    glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, 6, &buffer);
    glm::vec4 params[129];
    glGetNamedBufferSubData(buffer, 0, sizeof(params), params);
    bool coversDisk = params[0].x == count;
    for (int i = 0; i < count; ++i)
    {
        const glm::vec2 offset(params[i + 1]);
        coversDisk &= std::abs(glm::dot(offset, offset) - (i + .5f) / count) < 1e-5f;
    }
    Check(coversDisk, "RSM sample count preserves uniform disk coverage");
    Check(params[0].w == 2 * SceneRenderer::GetShadowSettings().OrthoHalfSize,
          "RSM receives the full light projection width in world units");
}

static void TestTemporalProjection()
{
    // 当前中心为 .25，邻域覆盖 [0,1]，让 .75 的历史色通过颜色限制，单独检验重投影。
    float current[9] = { 0, 1, 0, 1, .25f, 1, 0, 1, 0 };
    float history[9], depth[9];
    std::fill_n(history, 9, .75f);
    std::fill_n(depth, 9, .5f);
    GLuint textures[3];
    glCreateTextures(GL_TEXTURE_2D, 3, textures);
    const float* pixels[] = { current, history, depth };
    for (int i = 0; i < 3; ++i)
    {
        glTextureStorage2D(textures[i], 1, GL_R32F, 3, 3);
        glTextureSubImage2D(textures[i], 0, 0, 0, 3, 3, GL_RED, GL_FLOAT, pixels[i]);
        glTextureParameteri(textures[i], GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTextureParameteri(textures[i], GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTextureParameteri(textures[i], GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(textures[i], GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    FramebufferSpecification spec;
    spec.Width = spec.Height = 3;
    spec.Attachments = { FramebufferTextureFormat::RGBA16F };
    auto target = Framebuffer::Create(spec);
    auto shader = Shader::Create("assets/shaders/RSMTemporalResolve.glsl");
    struct TemporalParams
    {
        glm::mat4 InvViewProj{ 1 }, PrevViewProj{ 1 };
        float BlendAlpha = .9f;
        int ResetHistory = 0;
        float Padding[2]{};
    } params;
    static_assert(sizeof(TemporalParams) == 144);
    auto uniform = UniformBuffer::Create(sizeof(params), 8);
    GLuint vao, buffer;
    const float triangle[] = { -1, -1, 3, -1, -1, 3 };
    glCreateVertexArrays(1, &vao);
    glCreateBuffers(1, &buffer);
    glNamedBufferData(buffer, sizeof(triangle), triangle, GL_STATIC_DRAW);
    glVertexArrayVertexBuffer(vao, 0, buffer, 0, sizeof(float) * 2);
    glEnableVertexArrayAttrib(vao, 0);
    glVertexArrayAttribFormat(vao, 0, 2, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayAttribBinding(vao, 0, 0);
    const auto draw = [&]()
    {
        target->Bind();
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        for (int i = 0; i < 3; ++i) glBindTextureUnit(i, textures[i]);
        uniform->SetData(&params, sizeof(params));
        shader->Bind();
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        float color[4];
        glGetTextureSubImage(target->GetColorAttachmentRendererID(), 0, 1, 1, 0, 1, 1, 1,
                             GL_RGBA, GL_FLOAT, sizeof(color), color);
        return color[0];
    };
    Check(std::abs(draw() - .7f) < .001f, "temporal resolve blends valid reprojected history");
    params.ResetHistory = 1;
    Check(std::abs(draw() - .25f) < .001f, "temporal reset uses only the current frame");
    params.ResetHistory = 0;
    const glm::vec3 outside[] = { { 3, 0, 0 }, { 0, 3, 0 }, { 0, 0, 3 }, { 0, 0, -3 } };
    for (const auto& translation : outside)
    {
        params.PrevViewProj = glm::translate(glm::mat4(1), translation);
        const float value = draw();
        std::cout << "Previous clip offset: " << translation.x << ',' << translation.y << ','
                  << translation.z << " resolved=" << value << '\n';
        Check(std::abs(value - .25f) < .001f, "temporal resolve rejects all previous clip boundaries");
    }
    params.PrevViewProj = glm::mat4(1);
    params.PrevViewProj[3][3] = -1;
    Check(std::abs(draw() - .25f) < .001f, "temporal resolve rejects points behind the previous camera");
    glBindVertexArray(0);
    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &buffer);
    glDeleteTextures(3, textures);
    glEnable(GL_DEPTH_TEST);
    Check(glGetError() == GL_NO_ERROR, "temporal projection fixtures produce no GL errors");
}

int main()
{
    Log::Init();
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    auto* window = glfwCreateWindow(33, 17, "SceneRenderTests", nullptr, nullptr);
    if (!window) return 1;
    glfwMakeContextCurrent(window);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) return 1;
    RenderCommand::Init();
    Renderer2D::Init();
    Renderer3D::Init();
    SceneRenderer::Init();
    SceneRenderer::GetIBLEnabled() = false;
    SceneRenderer::GetShadowEnabled() = false;
    SceneRenderer::GetRSMSettings().Enabled = false;
    SceneRenderer::GetBloomSettings().Enabled = false;
    {
        FramebufferSpecification spec;
        spec.Width = 33; spec.Height = 17;
        spec.Attachments = { FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::RED_INTEGER,
                             FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::DEPTH32F };
        auto target = Framebuffer::Create(spec);
        SceneRenderer::SetMainSceneColorTarget(target->GetColorAttachmentRendererID(), 33, 17);
        Scene scene;
        auto& registry = scene.GetRegistry();
        auto sprite = registry.create();
        registry.emplace<TransformComponent>(sprite);
        registry.emplace<SpriteRendererComponent>(sprite, glm::vec4(1, 0, 0, .5f));
        EditorCamera camera(45, 33.0f / 17, .1f, 100);
        for (int mode = 0; mode < 2; ++mode)
        {
            for (int frame = 0; frame < 2; ++frame)
            {
                target->Bind();
                glDepthMask(GL_TRUE);
                glClearColor(0, 0, 0, 1);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                target->ClearAttachment(1, -1);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
                if (mode == 0) SceneRenderer::RenderRuntime(scene, glm::mat4(1), glm::mat4(1));
                else SceneRenderer::RenderScene(scene, camera);
                Check(glGetError() == GL_NO_ERROR, "editor/runtime SSAO accepts depth-only attachment");
                GLint alignment;
                glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
                Check(alignment == 4, "depth preview preserves upload row alignment");
                float color[4];
                glGetTextureSubImage(target->GetColorAttachmentRendererID(), 0, 16, 8, 0, 1, 1, 1,
                                     GL_RGBA, GL_FLOAT, sizeof(color), color);
                Check(std::abs(color[0] - .5f) < .01f, "sprite alpha blending survives consecutive scene frames");
                float normal[4];
                glGetTextureSubImage(target->GetColorAttachmentRendererID(2), 0, 16, 8, 0, 1, 1, 1,
                                     GL_RGBA, GL_FLOAT, sizeof(normal), normal);
                std::cout << "Sprite normal: " << normal[0] << ',' << normal[1] << ',' << normal[2] << '\n';
                Check(normal[0] == 0 && normal[1] == 0 && normal[2] == 0,
                      "2D sprites do not supply undefined normals to SSAO");
                unsigned char preview[4]{};
                glGetTextureSubImage(SceneRenderer::GetPrepassDepthTextureID(), 0, 16, 8, 0, 1, 1, 1,
                                     GL_RED, GL_UNSIGNED_BYTE, sizeof(preview), preview);
                const GLenum previewError = glGetError();
                std::cout << "Depth preview: mode=" << mode << " frame=" << frame
                          << " value=" << int(preview[0]) << " GL error=" << previewError << '\n';
                Check(previewError == GL_NO_ERROR && preview[0] == 0,
                      "single-depth geometry remains visible in depth preview");
            }
        }
        registry.destroy(sprite);
        auto light = registry.create();
        registry.emplace<TransformComponent>(light);
        auto& directional = registry.emplace<DirectionalLightComponent>(light);
        directional.Direction = glm::vec3(0);
        SceneRenderer::GetShadowEnabled() = true;
        target->Bind();
        SceneRenderer::RenderRuntime(scene, glm::mat4(1), glm::mat4(1));
        GLint lightBuffer;
        glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, LIGHT_UBO_BINDING, &lightBuffer);
        GPULightUBO lightHeader{};
        glGetNamedBufferSubData(lightBuffer, 0, sizeof(lightHeader), &lightHeader);
        Check(lightHeader.NumDirectionalLights == 0, "zero direction does not upload NaN light data");
        directional.Direction = glm::vec3(0, -1, 0);
        // 后建实体先被 EnTT 遍历；主绘制只支持一盏方向光，因此阴影不能改用第二盏。
        auto unshadowedLight = registry.create();
        registry.emplace<TransformComponent>(unshadowedLight);
        registry.emplace<DirectionalLightComponent>(unshadowedLight).CastShadow = false;
        target->Bind();
        SceneRenderer::RenderRuntime(scene, glm::mat4(1), glm::mat4(1));
        GLint cameraBuffer;
        glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, 0, &cameraBuffer);
        glm::vec4 shadowParams;
        glGetNamedBufferSubData(cameraBuffer, 144, sizeof(shadowParams), &shadowParams);
        Check(shadowParams.w == 0, "shadow selection matches the uploaded directional light");
        registry.destroy(unshadowedLight);
        SceneRenderer::GetRSMSettings().Enabled = true;
        for (int mode = 0; mode < 2; ++mode)
        {
            target->Bind();
            glDepthMask(GL_TRUE);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            if (mode == 0) SceneRenderer::RenderRuntime(scene, glm::mat4(1), glm::mat4(1));
            else SceneRenderer::RenderScene(scene, camera);
            Check(glGetError() == GL_NO_ERROR, "editor/runtime RSM accepts depth-only attachment");
            Check(SceneRenderer::GetRSMResolvedTextureID() != 0, "RSM temporal output exists");
        }
        auto& rsm = SceneRenderer::GetRSMSettings();
        glm::mat4 projection(1), cameraTransform(1);
        const auto render = [&]()
        {
            target->Bind();
            glDepthMask(GL_TRUE);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            SceneRenderer::RenderRuntime(scene, projection, cameraTransform);
        };
        render();
        Check(ReadHistoryReset() == 1, "editor to runtime transition resets RSM history");
        render();
        Check(ReadHistoryReset() == 0, "stable RSM inputs reuse history");
        cameraTransform = glm::translate(glm::mat4(1), glm::vec3(.1f, 0, 0));
        render();
        Check(ReadHistoryReset() == 0, "ordinary camera motion preserves RSM reprojection");
        projection[0][0] = 2;
        render();
        Check(ReadHistoryReset() == 1, "projection changes reset RSM history");
        rsm.Enabled = false;
        render();
        Check(SceneRenderer::GetRSMResolvedTextureID() == 0, "disabling RSM invalidates resolved history");
        rsm.Enabled = true;
        render();
        Check(ReadHistoryReset() == 1, "re-enabling RSM starts with the current frame");
        rsm.Intensity *= .5f;
        render();
        Check(ReadHistoryReset() == 1, "RSM intensity changes discard old lighting");
        rsm.SampleRadius *= .5f;
        render();
        Check(ReadHistoryReset() == 1, "RSM radius changes discard old lighting");
        for (int count : { 16, 32, 128 })
        {
            rsm.NumSamples = count;
            render();
            Check(ReadHistoryReset() == 1, "RSM sample count changes reset history");
            CheckRSMSamples(count);
        }
        directional.Color = glm::vec3(.5f, 1, 1);
        render();
        Check(ReadHistoryReset() == 1, "directional color changes discard old lighting");
        directional.Intensity *= 2;
        render();
        Check(ReadHistoryReset() == 1, "directional intensity changes discard old lighting");
        directional.Direction = glm::vec3(1, -1, 0);
        render();
        Check(ReadHistoryReset() == 1, "directional orientation changes discard old lighting");
        SceneRenderer::GetShadowSettings().OrthoHalfSize *= 2;
        render();
        Check(ReadHistoryReset() == 1, "light coverage changes reset RSM history");
        CheckRSMSamples(128);
        {
            Scene otherScene;
            auto& otherRegistry = otherScene.GetRegistry();
            auto otherLight = otherRegistry.create();
            otherRegistry.emplace<TransformComponent>(otherLight);
            otherRegistry.emplace<DirectionalLightComponent>(otherLight, directional);
            target->Bind();
            SceneRenderer::RenderRuntime(otherScene, projection, cameraTransform);
            Check(ReadHistoryReset() == 1, "another scene does not inherit RSM history");
        }
        Check(glGetError() == GL_NO_ERROR, "RSM history and sampling fixtures produce no GL errors");
        SceneRenderer::Shutdown();
        Check(SceneRenderer::GetPrepassDepthTextureID() == 0 && SceneRenderer::GetViewNormalTextureID() == 0,
              "shutdown clears borrowed preview texture names");
    }
    Renderer3D::Shutdown();
    Renderer2D::Shutdown();
    TestTemporalProjection();
    Check(glGetError() == GL_NO_ERROR, "scene GPU resources released without GL errors");
    glfwDestroyWindow(window);
    glfwTerminate();
    return s_Failures ? 1 : 0;
}
