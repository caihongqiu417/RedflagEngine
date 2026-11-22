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
#include <iomanip>

using namespace Redflag;
namespace fs = std::filesystem;
constexpr int Size = 64;

static void DrawSequence(VulkanContext& context, VulkanDraw& picking, const fs::path& folder)
{
    Scene scene;
    Entity fence = scene.CreateEntityWithUUID(UUID(1), "fence");
    ModelRendererComponent& model = fence.AddComponent<ModelRendererComponent>();
    model.ModelAsset = Model::Load((folder / "mask.gltf").string());
    if (!model.ModelAsset) throw std::runtime_error("Could not load the wire fixture");
    const int fenceID = int(static_cast<entt::entity>(fence));
    VulkanDraw surfaces(context, VulkanDraw::Output::LightingSurfaces);
    surfaces.Resize(Size, Size);
    VulkanComputeDevice device(context);
    std::vector<glm::vec4> albedo(Size * Size);
    std::vector<uint8_t> coverage(Size * Size);

    std::ofstream frames(folder / "frames.tsv");
    std::ofstream mask(folder / "raster.u8", std::ios::binary);
    std::ofstream ids(folder / "center-picks.i32", std::ios::binary);
    mask.exceptions(std::ios::badbit | std::ios::failbit);
    ids.exceptions(std::ios::badbit | std::ios::failbit);
    frames << std::setprecision(9) << "track\tframe\teye_x\teye_z\tprojection_x\tfence_id\n";
    const glm::mat4 projection = glm::perspective(glm::radians(45.f), 1.f, .02f, 5.f);
    auto frame = [&](const char* track, int index, float texelsPerPixel, float texelAtCenter) {
        // 铁丝网宽 2 个场景单位、原图宽 1024 个纹素。沿 Z 后退时，每个画面像素覆盖更多纹素。
        const glm::vec3 eye((texelAtCenter - 512.f) / 512.f, 0,
                            texelsPerPixel * Size * projection[0][0] / 1024.f);
        const glm::mat4 view = glm::lookAt(eye, eye + glm::vec3(0,0,-1), glm::vec3(0,1,0));
        auto draw = [&](CommandList& list) { SceneRenderer::DrawGeometry(scene, list, projection * view); };

        surfaces.DrawSurfaces(draw);
        device.Readback(surfaces.GetAlbedo(), albedo.data(), albedo.size() * sizeof(glm::vec4));
        for (size_t i = 0; i < albedo.size(); ++i) coverage[i] = albedo[i].a != 0.f;
        mask.write(reinterpret_cast<const char*>(coverage.data()), coverage.size());
        if (!context.BeginFrame()) throw std::runtime_error("Couldn't start the pick frame");
        picking.BeginScene();
        draw(picking);
        picking.EndScene();
        context.SwapBuffers();
        for (int x = 0; x < Size; ++x)
        {
            const int id = picking.ReadPixel(x, Size / 2).EntityID;
            ids.write(reinterpret_cast<const char*>(&id), sizeof(id));
        }
        frames << track << '\t' << index << '\t' << eye.x << '\t' << eye.z << '\t' << projection[0][0] << '\t' << fenceID << '\n';
    };

    // 放大时用同一行铁丝校验坐标。静止对照重复八帧，随后分别缓慢后退、保持距离水平移动。
    frame("magnified", 0, .5f, 512.375f);
    for (int i = 0; i < 8; ++i) frame("fixed", i, 1.5f, 512.375f);
    for (int i = 0; i <= 120; ++i) frame("zoom", i, 1.f + float(i) / 40.f, 512.375f);
    for (int i = 0; i <= 64; ++i) frame("pan", i, 1.5f, 512.375f + float(i) / 16.f);
}

int main(int argc, char** argv)
{
    if (argc != 2) return 1;
    const fs::path folder = fs::absolute(argv[1]);
    Log::Init();
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)> window(
        glfwCreateWindow(Size, Size, "Wire motion", nullptr, nullptr), glfwDestroyWindow);
    if (!window) return 1;
    try
    {
        VulkanContext context(window.get());
        context.Init();
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(context.GetPhysicalDevice(), &properties);
        std::ofstream(folder / "gpu.txt") << properties.deviceName << '\n' << properties.driverVersion << '\n';
        VulkanDraw picking(context);
        picking.Resize(Size, Size);
        RenderDevice::SetCurrent(&picking);
        RendererAPI::SetAPI(RendererAPI::API::Vulkan);
        DrawSequence(context, picking, folder);
        RenderDevice::SetCurrent(nullptr);
        context.WaitIdle();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
