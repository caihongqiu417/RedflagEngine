#include "rfpch.h"
#include "RenderDocCapture.h"
#include "Redflag/Renderer/RendererAPI.h"
#include "Redflag/Scene/Entity.h"
#include <GLFW/glfw3.h>
#include <fstream>
#include <iomanip>

// 本测试直接编译编辑器的实现，使用私有的 CollectModels，避免另写一份材质导出逻辑。
#include "../RedflagForge/src/LightingPreview.cpp"

using namespace Redflag;
namespace fs = std::filesystem;
constexpr int Width = 64;
const fs::path Work = "bin-int/Tests/gi-mask";

template<class T>
static std::vector<T> Read(VulkanComputeDevice& device, const Ref<ComputeResource>& buffer)
{
    std::vector<T> result(Width * Width);
    device.Readback(buffer, result.data(), result.size() * sizeof(T));
    return result;
}

template<class T>
static void Write(const std::string& filename, const std::vector<T>& values)
{
    std::ofstream file(Work / filename, std::ios::binary);
    file.exceptions(std::ios::badbit | std::ios::failbit);
    file.write(reinterpret_cast<const char*>(values.data()), values.size() * sizeof(T));
}

static Entity AddQuad(Scene& scene, const char* asset, float z, float scale, UUID id = UUID())
{
    Entity entity = scene.CreateEntityWithUUID(id, asset);
    TransformComponent& transform = entity.GetComponent<TransformComponent>();
    transform.Translation.z = z;
    transform.Scale = glm::vec3(scale);
    ModelRendererComponent& renderer = entity.AddComponent<ModelRendererComponent>();
    fs::path file(asset);
    if (!file.has_extension()) file += ".gltf";
    renderer.ModelAsset = Model::Load((Work / file).string());
    if (!renderer.ModelAsset) throw std::runtime_error("Could not load test quad");
    return entity;
}

static void SetMaterial(Entity& entity, const std::string& name)
{
    ModelRendererComponent& renderer = entity.GetComponent<ModelRendererComponent>();
    Ref<Material> material = renderer.ModelAsset->GetSubMeshes()[0].Mat->Clone();
    if (name == "red_map")
    {
        material->BaseColorMap.reset();
        material->AlphaMap = Texture2D::Create((Work / "opacity.png").string());
    }
    else if (name == "remove_base") material->BaseColorMap.reset();
    else if (name == "obj_remove") material->AlphaMap.reset();
    else if (name == "restore_base")
    {
        material->BaseColorMap.reset();
        renderer.OverrideMaterials = {material};
        renderer.OverrideMaterials.clear();
        return;
    }
    else if (name == "replace_base")
        material->BaseColorMap = Texture2D::Create((Work / "opacity.png").string());
    else if (name == "equal" || name == "below")
    {
        material->BaseColorMap.reset();
        material->BaseColor.a = .5f;
        material->AlphaCutoff = name == "equal" ? .5f : .501f;
    }
    else if (name == "entity_equal" || name == "entity_below")
    {
        material->BaseColor.a = .8f;
        renderer.Color.a = .625f;
        material->AlphaCutoff = name == "entity_equal" ? .5f : .501f;
    }
    else if (name == "red_linear" || name == "red_product")
    {
        material->AlphaMap = Texture2D::Create((Work / "half-red.png").string());
        material->AlphaCutoff = .4f;
        if (name == "red_product") material->BaseColor.a = .75f;
    }
    else return;
    renderer.OverrideMaterials = {material};
}

static void CheckShadows(VulkanContext& context, VulkanDraw& picking, VulkanComputeDevice& device, RenderDocCapture& capture)
{
    VulkanDraw surfaces(context, VulkanDraw::Output::LightingSurfaces);
    surfaces.Resize(Width, Width);
    static const uint32_t code[] =
#include "Platform/Vulkan/Shaders/PackSurfaces.comp.inc"
    ;
    const Ref<ComputePipeline> pack = device.CreatePipeline(code, sizeof(code),
        {ComputeBinding::Image, ComputeBinding::Image, ComputeBinding::Buffer}, sizeof(glm::mat4));
    const glm::vec3 eye(0, 0, 3);
    const glm::mat4 vp = glm::perspective(glm::radians(45.f), 1.f, .1f, 10.f) *
                         glm::lookAt(eye, glm::vec3(0), glm::vec3(0, 1, 0));
    const glm::mat4 inverse = glm::inverse(vp);
    std::ofstream picks(Work / "picks.tsv");
    picks << "case\tx\ty\tentity\n";

    for (const std::string name : {"receiver", "opaque", "mask", "backstop", "equal", "below", "entity_equal", "entity_below",
                                   "red_map", "red_linear", "red_product", "remove_base", "replace_base", "restore_base",
                                   "obj_original", "obj_remove"})
    {
        Scene scene;
        AddQuad(scene, "receiver", 0, 1);
        Entity sheet, behind;
        if (name != "receiver")
        {
            const char* asset = name == "opaque" ? "opaque" : (name.find("obj_") == 0 ? "fence.obj" : "mask");
            sheet = AddQuad(scene, asset, 1, .5f);
            SetMaterial(sheet, name);
        }
        if (name == "backstop") behind = AddQuad(scene, "receiver", .5f, .12f);
        GlobalIllumination gi(device, CollectModels(scene).Models, Width, Width);
        gi.SetLights({{glm::vec4(eye, 1), {10, 10, 10, 8}}});

        auto draw = [&](CommandList& list) { SceneRenderer::DrawGeometry(scene, list, vp); };
        if (!context.BeginFrame()) throw std::runtime_error("Could not start the picking pass");
        picking.BeginScene();
        draw(picking);
        picking.EndScene();
        context.SwapBuffers();
        for (glm::ivec2 point : {glm::ivec2(33, 29), {18, 29}, {6, 29}, {25, 25}})
            picks << name << '\t' << point.x << '\t' << point.y << '\t' << picking.ReadPixel(point.x, point.y).EntityID << '\n';
        if (name == "mask")
            for (int y = 25; y <= 38; ++y)
                for (int x = 25; x <= 38; ++x)
                    picks << "all_hole\t" << x << '\t' << y << '\t' << picking.ReadPixel(x, y).EntityID << '\n';

        if (name == "mask") capture.Begin();
        gi.SetCamera(vp, eye);
        Write(name + "-ray.bin", Read<GISurface>(device, gi.GetSurfaces()));
        auto rasterize = [&]() {
            surfaces.DrawSurfaces(draw);
            device.Dispatch(pack, {surfaces.GetAlbedo(), surfaces.GetNormalDepth(), gi.GetSurfaces()}, &inverse,
                            Width / 8, Width / 8);
        };
        rasterize();
        Write(name + "-raster.bin", Read<GISurface>(device, gi.GetSurfaces()));

        // 只重画接收光线的白墙。GI 的三角形仍保留布片及后方的小挡板，从墙上每个像素单独检查阴影。
        if (sheet) scene.DestroyEntity(sheet);
        if (behind) scene.DestroyEntity(behind);
        rasterize();
        gi.RenderDirect(false);
        Write(name + "-shadow.bin", Read<glm::vec4>(device, gi.GetDirectOutput()));
        if (name == "mask") capture.End();
    }
}

static void CheckReflections(VulkanComputeDevice& device)
{
    const glm::mat4 view(1);
    const glm::mat4 projection = glm::ortho(-1.f, 1.f, -1.f, 1.f, .01f, 15.f);
    ScreenEffects effects(device, Width, Width);
    effects.GetSettings().TraceScene = true;
    std::vector<glm::vec4> black(Width * Width, glm::vec4(0));
    const Ref<ComputeResource> input = device.CreateBuffer(black.size() * sizeof(black[0]), black.data());

    for (const std::string name : {"receiver", "mask", "opaque", "backstop"})
    {
        Scene scene;
        Entity sign = AddQuad(scene, "receiver", 1, 1);
        ModelRendererComponent& renderer = sign.GetComponent<ModelRendererComponent>();
        Ref<Material> glow = renderer.ModelAsset->GetSubMeshes()[0].Mat->Clone();
        glow->Emission = {4, .5f, .125f};
        glow->DoubleSided = true;
        renderer.OverrideMaterials = {glow};
        if (name != "receiver") AddQuad(scene, name == "opaque" ? "opaque" : "mask", .4f, .65f);
        if (name == "backstop") AddQuad(scene, "receiver", .7f, .09f);
        std::vector<GIModel> models = CollectModels(scene).Models;
        GIModel mirror;
        mirror.Transform = glm::scale(glm::translate(view, {0, 0, -.55f}), {2.4f, 2.4f, .1f});
        mirror.MaterialFactors = {{0, 1}};
        models.push_back(mirror);

        // 镜面位于相机前面，灯牌、裁剪片和小挡板都在相机背后，屏幕上的光只能由倒影带回。
        GlobalIllumination gi(device, models, Width, Width);
        gi.SetCamera(projection, glm::vec3(0));
        Write(name + "-mirror-primary.bin", Read<GISurface>(device, gi.GetSurfaces()));
        Ref<ComputeResource> image = effects.Render(ScreenEffect::MaterialReflections, gi, projection, view, 0, input);
        Write(name + "-mirror.bin", Read<glm::vec4>(device, image));
    }
}

static void CheckProbeBake(VulkanComputeDevice& device)
{
    Scene scene;
    // 同一房间在不同进程中保留原来的实体编号，比较烘焙时三组几何顺序一致。
    AddQuad(scene, "receiver", 0, 1, UUID(1));
    Entity fence = AddQuad(scene, "fence.obj", .5f, 1, UUID(2));
    Entity sign = AddQuad(scene, "receiver", 1, .5f, UUID(3));
    ModelRendererComponent& lamp = sign.GetComponent<ModelRendererComponent>();
    Ref<Material> glow = lamp.ModelAsset->GetSubMeshes()[0].Mat->Clone();
    glow->Emission = {4, .5f, .125f};
    lamp.OverrideMaterials = {glow};

    ModelRendererComponent& renderer = fence.GetComponent<ModelRendererComponent>();
    const Ref<Material> original = renderer.ModelAsset->GetSubMeshes()[0].Mat;
    const fs::path baked = Work / "open-fence.rfprobes";
    std::ofstream report(Work / "probe-load.tsv");
    report << "case\tloaded_original_bake\n";
    for (const std::string state : {"original", "same", "cutoff", "replace_opacity", "remove_opacity", "restore"})
    {
        Ref<Material> material = original->Clone();
        if (state == "cutoff") material->AlphaCutoff = .75f;
        if (state == "replace_opacity") material->AlphaMap = Texture2D::Create((Work / "half-red.png").string());
        if (state == "remove_opacity") material->AlphaMap.reset();
        renderer.OverrideMaterials = {material};
        if (state == "restore") renderer.OverrideMaterials.clear();
        GlobalIllumination gi(device, CollectModels(scene).Models, Width, Width);
        // 这里只有面光源。只算一轮，就可以把探针读到的红光直接解释成有多少条光线穿过了孔。
        gi.SetProbeFeedback(false);
        if (state == "original")
        {
            for (const std::string previous : {"previous-build", "other-opacity"})
            {
                const fs::path reference = Work / (previous + ".rfprobes");
                if (fs::exists(reference)) report << previous << '\t' << gi.LoadProbes(reference) << '\n';
            }
            gi.BakeProbes(baked);
        }
        report << state << '\t' << gi.LoadProbes(baked) << '\n';
        if (state == "remove_opacity") gi.BakeProbes(Work / "sealed-fence.rfprobes");
        if (state != "original" && state != "remove_opacity") continue;

        const GIReflectionResources resources = gi.GetReflectionResources();
        struct Room { glm::mat4 InverseVP; glm::vec4 Eye, Origin, Step; glm::uvec4 Image, Grid; } info;
        device.Readback(resources.SceneInfo, &info, sizeof(info));
        std::vector<glm::vec4> probes(size_t(info.Grid.x) * info.Grid.y * info.Grid.z * 6 * 2);
        device.Readback(resources.Probes, probes.data(), probes.size() * sizeof(probes[0]));
        Write(state + "-probes.bin", probes);
        // 取孔正下方、最靠近房间中央的一组探针，查看朝向灯牌的 +Z 半球。坐标由 GPU 提供的网格间距换算。
        const glm::uvec3 cell(info.Grid.x / 2, info.Grid.y / 2, 2);
        const glm::vec3 position = glm::vec3(info.Origin) + glm::vec3(cell) * glm::vec3(info.Step);
        const size_t probe = (cell.z * info.Grid.y + cell.y) * info.Grid.x + cell.x;
        const glm::vec4 light = probes[(probe * 6 + 4) * 2];
        std::ofstream point(Work / (state + "-probe.tsv"));
        point << std::setprecision(9) << "cell_x\tcell_y\tcell_z\tworld_x\tworld_y\tworld_z\tred\tgreen\tblue\n"
              << cell.x << '\t' << cell.y << '\t' << cell.z << '\t' << position.x << '\t' << position.y << '\t'
              << position.z << '\t' << light.r << '\t' << light.g << '\t' << light.b << '\n';
    }
}

int main(int argc, char** argv)
{
    Log::Init();
    RenderDocCapture capture(argc >= 2 ? argv[1] : nullptr, argc == 3 ? argv[2] : "output/validation/gi-mask/Mask",
        "Cutout sheet and point light at the camera", "64 x 64. Primary hits, rasterized material, receiver point shadows.");
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)> window(
        glfwCreateWindow(Width, Width, "GI cutout", nullptr, nullptr), glfwDestroyWindow);
    if (!window) return 1;
    try
    {
        VulkanContext context(window.get());
        context.Init();
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(context.GetPhysicalDevice(), &properties);
        std::ofstream(Work / "device.txt") << properties.deviceName << '\n' << properties.apiVersion << '\n'
                                          << properties.driverVersion << '\n';
        VulkanDraw picking(context);
        picking.Resize(Width, Width);
        VulkanComputeDevice device(context);
        RenderDevice::SetCurrent(&picking);
        RendererAPI::SetAPI(RendererAPI::API::Vulkan);
        CheckShadows(context, picking, device, capture);
        CheckReflections(device);
        CheckProbeBake(device);
        RenderDevice::SetCurrent(nullptr);
        context.WaitIdle();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
