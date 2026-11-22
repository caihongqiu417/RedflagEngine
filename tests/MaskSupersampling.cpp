#include "rfpch.h"
#include "Platform/Vulkan/VulkanComputeDevice.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Platform/Vulkan/VulkanDraw.h"
#include "Redflag/Renderer/RendererAPI.h"
#include "Redflag/Renderer/SceneRenderer.h"
#include "Redflag/Scene/Entity.h"
#include "Redflag/Scene/Scene.h"
#include <GLFW/glfw3.h>
#include <fstream>

using namespace Redflag;
namespace fs = std::filesystem;
constexpr int Size = 64;

static Ref<Material> AddPlane(Scene& scene, const fs::path& path, const char* name, float z)
{
    Entity plane = scene.CreateEntity(name);
    plane.GetComponent<TransformComponent>().Translation.z = z;
    if (z < 0.f) plane.GetComponent<TransformComponent>().Scale = {4.f, 4.f, 1.f};
    ModelRendererComponent& component = plane.AddComponent<ModelRendererComponent>();
    component.ModelAsset = Model::Load(path.string());
    if (!component.ModelAsset) throw std::runtime_error("Cannot read test model");
    return component.ModelAsset->GetSubMeshes().at(0).Mat;
}

static void Measure(VulkanContext& context, const fs::path& folder)
{
    VulkanDraw surfaces(context, VulkanDraw::Output::LightingSurfaces);
    VulkanDraw emission(context, VulkanDraw::Output::Emission);
    RenderDevice::SetCurrent(&surfaces);
    RendererAPI::SetAPI(RendererAPI::API::Vulkan);
    Scene scene;
    Ref<Material> back = AddPlane(scene, folder / "receiver.gltf", "background", -.1f);
    Ref<Material> front = AddPlane(scene, folder / "mask.gltf", "wire", 0.f);
    VulkanComputeDevice device(context);

    std::ifstream shader(folder / "resolve.spv", std::ios::binary | std::ios::ate);
    if (!shader) throw std::runtime_error("Cannot open test resolve shader");
    const size_t byteCount = static_cast<size_t>(shader.tellg());
    std::vector<uint32_t> code(byteCount / sizeof(uint32_t));
    shader.seekg(0);
    shader.read(reinterpret_cast<char*>(code.data()), byteCount);
    Ref<ComputePipeline> resolve = device.CreatePipeline(code.data(), byteCount,
        { ComputeBinding::Image, ComputeBinding::Image, ComputeBinding::Buffer }, sizeof(int));
    Ref<ComputeResource> image = device.CreateImage(Size, Size);
    std::vector<glm::vec4> output(Size * Size);

    std::ifstream sequence(folder / "camera.txt");
    std::vector<glm::vec3> cameras;
    float x, z;
    while (sequence >> x >> z) cameras.emplace_back(x, 0.f, z);
    if (cameras.empty()) throw std::runtime_error("Cannot read camera positions");
    const glm::mat4 projection = glm::perspective(glm::radians(45.f), 1.f, .02f, 5.f);
    const glm::vec3 foreground[] = { {4.f,.125f,.25f}, {.0625f,.75f,2.f} };
    const glm::vec3 background[] = { {.125f,.375f,1.5f}, {2.f,1.f,.0625f} };
#ifdef RF_TEST_MASK_SSAA
    static const uint32_t displayShader[] =
#include "Shaders/LightingPreview.comp.inc"
    ;
    struct DisplaySettings { uint32_t Gray, SampleSide, Filmic; float Exposure; };
    Ref<ComputePipeline> display = device.CreatePipeline(displayShader, sizeof(displayShader),
        {ComputeBinding::Buffer, ComputeBinding::Image}, sizeof(DisplaySettings));
    Ref<ComputeResource> panel = device.CreateImage(Size, Size);
    std::vector<glm::vec4> panelColor(Size * Size);
    surfaces.SetSubpixelMask(true);
    emission.SetSubpixelMask(true);
    const std::vector<int> scales = {2};
    const std::string prefix = "production-";
#else
    const std::vector<int> scales = {1, 2, 4, 8};
    const std::string prefix;
#endif
    for (int scale : scales)
    {
        const int width = Size * scale;
        surfaces.Resize(width, width);
        emission.UseSceneDepth(surfaces);
        std::vector<glm::vec4> raw(width * width);
        Ref<ComputeResource> full = device.CreateBuffer(size_t(width) * width * sizeof(glm::vec4));
        for (int palette = 0; palette < 2; ++palette)
        {
            front->Emission = foreground[palette];
            back->Emission = background[palette];
            const std::string label = prefix + std::to_string(scale) + "-" + std::to_string(palette);
            std::ofstream file(folder / (label + ".f32"), std::ios::binary);
            file.exceptions(std::ios::badbit | std::ios::failbit);
#ifdef RF_TEST_MASK_SSAA
            std::ofstream displayed(folder / (label + "-panel.f32"), std::ios::binary);
            displayed.exceptions(std::ios::badbit | std::ios::failbit);
#endif
            for (size_t i = 0; i < cameras.size(); ++i)
            {
                const glm::vec3 eye = cameras[i];
                const glm::mat4 vp = projection * glm::lookAt(eye, eye + glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
                auto draw = [&](CommandList& list) { SceneRenderer::DrawGeometry(scene, list, vp); };
                // 两个平面分别参加深度测试。只给仍可见的表面画自发光，射穿小孔的位置会得到后面板的颜色。
                surfaces.DrawSurfaces(draw);
                emission.DrawSurfaces(draw);
                device.Dispatch(resolve, {emission.GetEmission(), image, full}, &scale, Size / 8, Size / 8);
                device.Readback(image, output.data(), output.size() * sizeof(glm::vec4));
                file.write(reinterpret_cast<const char*>(output.data()), output.size() * sizeof(glm::vec4));
#ifdef RF_TEST_MASK_SSAA
                const DisplaySettings settings{0, uint32_t(scale), 0, 1.f};
                device.Dispatch(display, {full, panel}, &settings, Size / 8, Size / 8);
                device.Readback(panel, panelColor.data(), panelColor.size() * sizeof(glm::vec4));
                displayed.write(reinterpret_cast<const char*>(panelColor.data()), panelColor.size() * sizeof(glm::vec4));
#endif

                if (i == 0 || i == 69 || i + 1 == cameras.size())
                {
                    // 把三张高分辨率原图一并读回，独立核对 GPU 平均值和前后景每个样本的深度归属。
                    device.Readback(emission.GetEmission(), raw.data(), raw.size() * sizeof(glm::vec4));
                    std::ofstream witness(folder / (label + "-raw-" + std::to_string(i) + ".f32"), std::ios::binary);
                    witness.write(reinterpret_cast<const char*>(raw.data()), raw.size() * sizeof(glm::vec4));
                }
            }
        }
    }
    RenderDevice::SetCurrent(nullptr);
}

int main(int argc, char** argv)
{
    if (argc != 2) return 1;
    fs::path folder = fs::absolute(argv[1]);
    Log::Init();
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)> window(
        glfwCreateWindow(Size, Size, "Mask supersampling", nullptr, nullptr), glfwDestroyWindow);
    if (!window) return 1;
    try
    {
        VulkanContext context(window.get());
        context.Init();
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(context.GetPhysicalDevice(), &properties);
        std::ofstream(folder / "gpu.txt") << properties.deviceName << '\n' << properties.driverVersion << '\n';
        Measure(context, folder);
        context.WaitIdle();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
