#include "rfpch.h"
#include "RenderDocCapture.h"
#include "Platform/OpenGL/OpenGLCommandList.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Platform/Vulkan/VulkanDraw.h"
#include "Redflag/Assets/AssetManager.h"
#include "Redflag/Renderer/Framebuffer.h"
#include "Redflag/Renderer/RenderCommand.h"
#include "Redflag/Renderer/Renderer2D.h"
#include "Redflag/Renderer/Renderer3D.h"
#include "Redflag/Renderer/RendererAPI.h"
#include "Redflag/Scene/SceneSerializer.h"
#include "Redflag/Scene/Entity.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <spdlog/sinks/ostream_sink.h>

using namespace Redflag;
namespace fs = std::filesystem;

static void Require(bool passed, const char* message)
{
    if (!passed) throw std::runtime_error(message);
}

int main(int argc, char** argv)
{
    if (argc < 3 || argc > 4) return 1;
    const bool vulkan = std::string(argv[1]) == "Vulkan", automatic = std::string(argv[2]) == "auto";
    const bool scenePasses = std::string(argv[1]) == "OpenGLScene";
    const std::string label = std::string(argv[1]) + "-" + argv[2];
    const char* outputOverride = std::getenv("RF_LOD_OUTPUT");
    const fs::path output = outputOverride ? outputOverride : "../output/profiling/mesh-lod";
    fs::create_directories(output);
    RenderDocCapture capture(argc == 4 ? argv[3] : nullptr, fs::path("../output/captures/local") / ("LOD-" + label),
        "Stanford Bunny / " + label, "384x384 orthographic view. Bunny spans 10 percent of view height. Ground unchanged.");
    Log::Init();
    std::ostringstream errors;
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(errors);
    sink->set_level(spdlog::level::err);
    Log::GetCoreLogger()->sinks().push_back(sink);
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    if (vulkan) glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    else
    {
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    }
    constexpr int size = 384;
    auto window = std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)>(
        glfwCreateWindow(size, size, "MeshLODTests", nullptr, nullptr), glfwDestroyWindow);
    if (!window) return 1;
    try
    {
        Scope<VulkanContext> context;
        Scope<VulkanDraw> vk;
        Scope<OpenGLCommandList> gl;
        Ref<Framebuffer> target;
        if (vulkan)
        {
            RendererAPI::SetAPI(RendererAPI::API::Vulkan);
            context = CreateScope<VulkanContext>(window.get());
            context->Init();
            vk = CreateScope<VulkanDraw>(*context);
            RenderDevice::SetCurrent(vk.get());
            vk->Resize(size, size);
        }
        else
        {
            glfwMakeContextCurrent(window.get());
            Require(gladLoadGLLoader((GLADloadproc)glfwGetProcAddress), "Cannot initialize OpenGL");
            gl = CreateScope<OpenGLCommandList>();
            FramebufferSpecification spec;
            spec.Width = spec.Height = size;
            spec.Attachments = {FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::RED_INTEGER,
                                FramebufferTextureFormat::Depth};
            if (scenePasses)
            {
                RenderCommand::Init();
                Renderer2D::Init();
                Renderer3D::Init();
                SceneRenderer::Init();
                SceneRenderer::GetZPrepassEnabled() = true;
                SceneRenderer::GetShadowEnabled() = true;
                SceneRenderer::GetRSMSettings().Enabled = true;
                SceneRenderer::GetIBLEnabled() = false;
                spec.Attachments = {FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::RED_INTEGER,
                                    FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::DEPTH32F};
            }
            target = Framebuffer::Create(spec);
            if (scenePasses) SceneRenderer::SetMainSceneColorTarget(target->GetColorAttachmentRendererID(), size, size);
            glEnable(GL_DEPTH_TEST);
        }

        auto scene = CreateRef<Scene>();
        scene->OnViewportResize(size, size);
        Require(SceneSerializer(scene).Deserialize("assets/scenes/Reference-StanfordBunny.redflag"), "Cannot read Stanford scene");
        fs::create_directories("../tmp/mesh-lod");
        const auto save = "../tmp/mesh-lod/" + label + ".redflag";
        SceneSerializer(scene).Serialize(save);
        scene = CreateRef<Scene>();
        scene->OnViewportResize(size, size);
        Require(SceneSerializer(scene).Deserialize(save), "Cannot reopen round-tripped scene");
        auto rabbit = scene->GetEntityByUUID(UUID(1));
        auto& model = rabbit.GetComponent<ModelRendererComponent>();
        Require(model.LODs.size() == 2 && model.LODs[0].ScreenHeight == .25f && model.LODs[1].ScreenHeight == .12f,
                "LOD thresholds changed on save");
        model.ModelAsset = AssetManager::Load<Model>(model.ModelID);
        Require(model.ModelAsset != nullptr, "Run python tests/fetch_stanford.py first");
        if (!automatic) model.LODs.clear();
        const auto world = rabbit.GetComponent<TransformComponent>().GetTransform();
        const auto view = glm::lookAt(glm::vec3(0,0,5), glm::vec3(0), glm::vec3(0,1,0));
        const float baseHeight = model.ModelAsset->GetScreenHeight(glm::ortho(-1.f,1.f,-1.f,1.f,.1f,50.f) * view * world);
        CommandList& commands = vk ? static_cast<CommandList&>(*vk) : static_cast<CommandList&>(*gl);
        std::ofstream samples(output / (label + ".csv"));
        samples << "height,lod,triangles,entity,picked,record_us\n";
        auto render = [&](float height, size_t expected, bool record, bool measure) {
            const float half = baseHeight / height;
            const auto projection = glm::ortho(-half, half, -half, half, .1f, 50.f);
            const auto vp = projection * view;
            if (record) capture.Begin();
            if (vk)
            {
                Require(context->BeginFrame(), "Cannot acquire frame");
                vk->BeginScene();
            }
            else
            {
                target->Bind();
                glDepthMask(GL_TRUE);
                glDepthFunc(GL_LESS);
                glDisable(GL_CULL_FACE);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                target->ClearAttachment(1, -1);
            }
            const auto start = std::chrono::steady_clock::now();
            if (scenePasses) SceneRenderer::RenderRuntime(*scene, projection, glm::inverse(view));
            else SceneRenderer::DrawGeometry(*scene, commands, vp);
            const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
            if (vk) { vk->EndScene(); context->SwapBuffers(); }
            else glFinish();
            if (record) capture.End();
            if (target) target->Bind();
            const int picked = vk ? vk->ReadPixel(size/2, size/2).EntityID : target->ReadPixel(1, size/2, size/2);
            const int id = int(static_cast<entt::entity>(rabbit));
            Require(picked == id, "LOD changed pick result at the rabbit's torso");
            const auto level = model.ActiveLOD;
            Require(level == (automatic ? expected : 0), "LOD hysteresis selected the wrong level");
            const auto& mesh = (level ? model.LODs[level - 1].ModelAsset : model.ModelAsset)->GetSubMeshes()[0].MeshData;
            const int triangles[] = {16301, 3851, 948};
            Require(mesh->GetIndexCount() == 3 * triangles[level], "Incorrect geometry for this level");
            if (measure) samples << height << ',' << level << ',' << triangles[level] << ',' << id << ',' << picked << ',' << us << '\n';
        };
        // 两次经过 24% 和 12% 时所处档位不同，可以同时检验拉远与拉近的切换方向。
        for (auto [height, level] : std::vector<std::pair<float,int>>{
                {.5f,0},{.24f,0},{.22f,1},{.24f,1},{.12f,1},{.10f,2},{.12f,2},{.14f,1},{.28f,0}})
            render(height, level, false, true);
        for (int i = 0; i < 4; ++i) render(.10f, 2, false, false);
        for (int i = 0; i < 32; ++i) render(.10f, 2, false, true);
        render(.10f, 2, true, false);
        if (automatic)
        {
            model.LODs.back().ModelAsset.reset();
            model.LODs.back().ModelID = 0;
            render(.10f, 1, false, false);  // The next finer asset remains selectable.
            const auto nearProjection = glm::perspective(glm::radians(60.f), 1.f, .1f, 50.f) * world;
            Require(model.ModelAsset->GetScreenHeight(nearProjection) > 1.1f, "Near-plane intersection lost detail");
            model.LODs.clear();
            render(.10f, 0, false, false);
        }

        if (vk) context->WaitIdle();
        scene.reset();
        AssetManager::ClearAll();
        if (scenePasses)
        {
            SceneRenderer::Shutdown();
            Renderer3D::Shutdown();
            Renderer2D::Shutdown();
        }
        RenderDevice::SetCurrent(nullptr);
        Require(errors.str().empty(), errors.str().c_str());
        if (gl) Require(glGetError() == GL_NO_ERROR, "OpenGL error");
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
    window.reset();
    glfwTerminate();
    std::cout << label << " passed\n";
    return 0;
}
