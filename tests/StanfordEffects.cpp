#include "rfpch.h"
#include "Platform/Vulkan/VulkanComputeDevice.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Platform/Vulkan/VulkanDraw.h"
#include "Redflag/Assets/AssetManager.h"
#include "Redflag/Renderer/GI/GlobalIllumination.h"
#include "Redflag/Renderer/ScreenSpace/ScreenEffects.h"
#include "Redflag/Scene/Entity.h"
#include "Redflag/Scene/Scene.h"
#include "Redflag/Scene/SceneSerializer.h"
#include "RenderDocCapture.h"
#include <GLFW/glfw3.h>

using namespace Redflag;
namespace fs = std::filesystem;
constexpr uint32_t Width = 288, Height = 192;

static void Run(VulkanComputeDevice& device, const std::string& name, RenderDocCapture* capture = nullptr)
{
    auto scene = CreateRef<Scene>();
    scene->OnViewportResize(Width, Height);
    if (!SceneSerializer(scene).Deserialize("assets/scenes/Reference-" + name + ".redflag"))
        throw std::runtime_error("Cannot open " + name);

    auto camera = scene->GetPrimaryCameraEntity();
    const auto& pose = camera.GetComponent<TransformComponent>();
    const glm::mat4 view = glm::inverse(pose.GetTransform());
    const glm::mat4 projection = camera.GetComponent<CameraComponent>().Camera.GetProjection();

    std::vector<GIModel> models;
    auto& registry = scene->GetRegistry();
    glm::vec3 modelColor;
    float roughness = 0;
    registry.view<TransformComponent, ModelRendererComponent>().each([&](auto& transform, auto& model) {
        model.ModelAsset = AssetManager::Load<Model>(model.ModelID);
        if (!model.ModelAsset || model.OverrideMaterials.empty())
            throw std::runtime_error("Missing Stanford model or its saved material");
        GIModel shape{model.ModelAsset->GetFilePath(), transform.GetTransform(), glm::vec3(model.Color)};
        for (const auto& material : model.OverrideMaterials)
            shape.MaterialColors.push_back(glm::vec4(glm::vec3(material->BaseColor), 1));
        modelColor = glm::vec3(shape.MaterialColors.front()) * shape.Tint;
        roughness = model.OverrideMaterials.front()->Roughness;
        models.push_back(std::move(shape));
    });
    registry.view<TransformComponent, MeshRendererComponent>().each([&](auto& transform, auto& floor) {
        models.push_back({{}, transform.GetTransform(), glm::vec3(floor.Mat->BaseColor)});
    });

    std::vector<GILight> lights;
    float range = 0;
    registry.view<TransformComponent, PointLightComponent>().each([&](auto& transform, auto& light) {
        lights.push_back({glm::vec4(transform.Translation, 1), glm::vec4(light.Color * light.Intensity, 0)});
        range = light.Radius;
    });
    if (models.size() != 2 || lights.size() != 1)
        throw std::runtime_error("Expected one scanned mesh, one floor and one point light");

    const fs::path out = fs::path("../tmp/stanford-effects") / name;
    fs::create_directories(out);
    auto save = [&](const std::string& file, const Ref<ComputeResource>& buffer, size_t stride = sizeof(glm::vec4)) {
        std::vector<char> values(size_t(Width) * Height * stride);
        device.Readback(buffer, values.data(), values.size());
        std::ofstream stream(out / (file + ".f32"), std::ios::binary);
        stream.write(values.data(), values.size());
        if (!stream) throw std::runtime_error("Cannot save " + file);
    };

    GlobalIllumination gi(device, models, Width, Height);
    gi.SetCamera(projection * view, pose.Translation);
    gi.SetLights(lights);
    gi.SetSeed(137);
    ScreenEffects screen(device, Width, Height);
    screen.GetSettings().Roughness = roughness;
    screen.GetSettings().LightRange = range;
    if (capture)
    {
        gi.Render(GIMethod::DirectReference);
        screen.Render(ScreenEffect::TAA, gi, projection, view, 0);
        capture->Begin();
        gi.Render(GIMethod::DDGI, 16);
        gi.Render(GIMethod::ReSTIRGI);
        gi.Render(GIMethod::ReSTIRDI);
        gi.Render(GIMethod::DirectReference);
        screen.Render(ScreenEffect::GTAO, gi, projection, view, 0);
        screen.Render(ScreenEffect::StochasticReflections, gi, projection, view, 0);
        screen.Render(ScreenEffect::ClusteredLighting, gi, projection, view, 0);
        screen.Render(ScreenEffect::TAA, gi, projection, view, 1);
        capture->End();
        return;
    }
    std::ofstream metadata(out / "scene.txt");
    metadata << Width << ' ' << Height << '\n' << modelColor.r << ' ' << modelColor.g << ' ' << modelColor.b
             << '\n' << range << ' ' << roughness << '\n';

    // 先保存可见表面，再用同一组顶点和灯光分别计算直接光、一次反弹参考和三种采样结果。
    save("surfaces", gi.GetSurfaces(), sizeof(GISurface));
    auto render = [&](const char* label, GIMethod method, uint32_t samples, uint32_t frames) {
        for (uint32_t i = 0; i < frames; ++i) gi.Render(method, samples);
        save(label, gi.GetOutput());
    };
    render("indirect-reference", GIMethod::IndirectReference, 128, 1);
    render("ddgi", GIMethod::DDGI, 16, 8);
    render("restir-gi", GIMethod::ReSTIRGI, 1, 8);
    render("restir-di", GIMethod::ReSTIRDI, 1, 8);
    render("direct", GIMethod::DirectReference, 1, 1);
    save("ao", screen.Render(ScreenEffect::GTAO, gi, projection, view, 0));
    save("ssr", screen.Render(ScreenEffect::StochasticReflections, gi, projection, view, 0));
    save("cluster", screen.Render(ScreenEffect::ClusteredLighting, gi, projection, view, 0));

    // 八个亚像素采样位置各留一张直接光图，离线均值供最后一帧 TAA 对照。
    for (uint32_t f = 0; f < 8; ++f)
    {
        auto shifted = ScreenEffects::JitterProjection(projection, f, Width, Height);
        gi.SetCamera(shifted * view, pose.Translation);
        gi.Render(GIMethod::DirectReference);
        save("sample-" + std::to_string(f), gi.GetOutput());
        auto taa = screen.Render(ScreenEffect::TAA, gi, shifted, view, f, {}, &projection);
        if (f == 7) save("taa", taa);
    }
    std::cout << "Read back seven Vulkan effects for " << name << '\n';
}

int main(int argc, char** argv)
{
    if (argc != 1 && !(argc == 4 && std::string(argv[1]) == "--capture")) return 2;
    std::string sceneName = argc == 4 ? argv[3] : "";
    RenderDocCapture capture(argc == 4 ? argv[2] : nullptr, fs::path("../output/profiling/stanford") / sceneName,
        sceneName + " / seven Vulkan effects", "288x192. Model and camera loaded from Reference-" + sceneName + ".redflag.");
    Log::Init();
    if (!glfwInit()) return 1;
    int result = 0;
    try
    {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)> window(
            glfwCreateWindow(64, 64, "Stanford scene tests", nullptr, nullptr), glfwDestroyWindow);
        if (!window) throw std::runtime_error("Cannot create Stanford test window");
        VulkanContext context(window.get());
        context.Init();
        VulkanDraw draw(context);
        VulkanComputeDevice device(context);
        RenderDevice::SetCurrent(&draw);

        try
        {
            if (argc == 4)
            {
                if (!context.BeginFrame()) throw std::runtime_error("Cannot acquire capture window");
                context.BeginWindowPass();
                context.EndWindowPass();
                context.SwapBuffers();
                Run(device, sceneName, &capture);
            }
            else
                for (auto name : {"StanfordBunny", "StanfordDragon", "HappyBuddha"}) Run(device, name);
        }
        catch (const std::exception& e)
        {
            std::cerr << e.what() << '\n';
            result = 1;
        }
        AssetManager::ClearAll();
        RenderDevice::SetCurrent(nullptr);
        VkPhysicalDeviceProperties gpu{};
        vkGetPhysicalDeviceProperties(context.GetPhysicalDevice(), &gpu);
        std::ofstream("../tmp/stanford-effects/device.txt") << gpu.deviceName;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        result = 1;
    }
    glfwTerminate();
    return result;
}
