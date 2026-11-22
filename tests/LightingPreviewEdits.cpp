#include "rfpch.h"
#include "LightingPreview.h"
#include "Platform/Vulkan/VulkanDraw.h"
#include "Redflag/ImGui/VulkanImGui.h"
#include "Redflag/Renderer/RendererAPI.h"
#include "Redflag/Renderer/SceneRenderer.h"
#include "Redflag/Scene/Scene.h"
#include "Redflag/Scene/Entity.h"
#include "imgui.h"
#include <GLFW/glfw3.h>
#include <fstream>
#include <iostream>

using namespace Redflag;
namespace fs = std::filesystem;
constexpr int ViewportSize = 48;

static void Check(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

static std::string Read(const fs::path& file)
{
    std::ifstream input(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

// 读取正式渲染设置面板生成的文字。bake 为 true 时，在这一帧向 ImGui 输入烘焙快捷键。
static std::string PanelText(LightingPreview& preview, const fs::path& file, bool bake = false)
{
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(900, 1000);
    io.DeltaTime = 1.f / 60;
    if (bake)
    {
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        io.AddKeyEvent(ImGuiMod_Shift, true);
        io.AddKeyEvent(ImGuiKey_B, true);
    }
    ImGui::NewFrame();
    ImGui::SetNextWindowSize({850, 980});
    ImGui::Begin("Lighting");
    std::ofstream(file).close(); // Dear ImGui 会把记录追加到文件末尾。
    ImGui::LogToFile(20, file.string().c_str());
    preview.DrawSettings();
    ImGui::LogFinish();
    ImGui::End();
    ImGui::Render();
    if (bake)
    {
        io.AddKeyEvent(ImGuiKey_B, false);
        io.AddKeyEvent(ImGuiMod_Shift, false);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
    }
    return Read(file);
}

static void CheckScene(VulkanContext& context, VulkanDraw& draw, const fs::path& work, std::ostream& report)
{
    Scene scene;
    Entity panel = scene.CreateEntity("test panel");
    ModelRendererComponent& model = panel.AddComponent<ModelRendererComponent>();
    model.ModelAsset = Model::Load("tests/fixtures/TextureQuad.gltf");
    Check(model.ModelAsset && model.ModelAsset->GetSubMeshes().size() == 1, "Expected the textured test panel");
    auto addLight = [&]() {
        Entity light = scene.CreateEntity("point light");
        light.GetComponent<TransformComponent>().Translation = {0, 1, 2};
        light.AddComponent<PointLightComponent>().Intensity = 10;
        return light;
    };
    Entity light = addLight();

    const fs::path entry = work / "room.redflag";
    const fs::path probes = fs::path(entry).concat(".rfprobes");
    std::ofstream(fs::path(entry).concat(".render.yaml")) << "Shading: Lit\nIndirect: DDGI\nTAA: false\nGTAO: false\n";
    const glm::vec3 eye{0, 0, 3};
    const glm::mat4 view = glm::lookAt(eye, glm::vec3(0), glm::vec3(0, 1, 0));
    const glm::mat4 projection = glm::perspective(glm::radians(45.f), 1.f, .1f, 30.f);
    LightingPreview preview(context, draw.GetSampler());
    preview.Load(entry);
    preview.BakeProbes(scene);
    const std::string originalBake = Read(probes);
    const auto originalWrite = fs::last_write_time(probes);
    Check(!originalBake.empty(), "No bake was saved");

    const int panelID = int(static_cast<entt::entity>(panel));
    auto measure = [&](const char* label, const char* status, int expectedID, bool expectRaster) {
        // 这个计时段包住正式表面绘制与 PackSurfaces dispatch，可区分重画与直接复用上一张光照图。
        const fs::path trace = work / "frame.json";
        RF_PROFILE_BEGIN_SESSION("lighting edits", trace.string());
        VkDescriptorSet image = preview.Render(scene, projection, view, eye, ViewportSize, ViewportSize, false);
        RF_PROFILE_END_SESSION();
        const bool rasterized = Read(trace).find("Rasterize lighting surfaces") != std::string::npos;
        const std::string panelText = PanelText(preview, work / "panel.txt");
        Check(panelText.find(status) != std::string::npos, std::string(label) + ": " + panelText);
        Check(rasterized == expectRaster, std::string(label) + ": unexpected surface render");
        Check(bool(image) == (expectedID != -1), std::string(label) + ": wrong lighting image");

        Check(context.BeginFrame(), "Unable to start picking frame");
        draw.BeginScene();
        SceneRenderer::DrawGeometry(scene, draw, projection * view);
        draw.EndScene();
        context.SwapBuffers();
        VulkanDraw::Pixel pixel = draw.ReadPixel(ViewportSize / 2, ViewportSize / 2);
        Check(pixel.EntityID == expectedID, std::string(label) + ": wrong entity ID");
        const bool keptBake = originalBake == Read(probes) && originalWrite == fs::last_write_time(probes);
        Check(keptBake, std::string(label) + ": an earlier request overwrote the probe file");
        report << label << '\t' << status << '\t' << rasterized << '\t' << bool(image) << '\t' << pixel.EntityID
               << '\t' << int(pixel.Color.r) << ',' << int(pixel.Color.g) << ',' << int(pixel.Color.b) << '\t' << keptBake << '\n';
        return glm::u8vec3(pixel.Color);
    };

    report << "stage\tDDGI_panel\trasterized_lighting_surfaces\thas_lighting_image\tpicked_id\tbase_rgb8\toriginal_bake_unchanged\n";
    glm::u8vec3 original = measure("original", "已烘焙", panelID, true);
    measure("unchanged", "已烘焙", panelID, false);

    // 法线贴图只改变表面接收光照的方向，即使镜头不动也要重画。再给同一张贴图换像素，重复检查。
    Ref<Material> relief = model.ModelAsset->GetSubMeshes()[0].Mat->Clone();
    relief->NormalMap = Texture2D::Create(1, 1);
    glm::u8vec4 normal{255, 128, 128, 255};
    relief->NormalMap->SetData(&normal, sizeof(normal));
    model.OverrideMaterials = {relief};
    measure("add_normal_map", "已烘焙", panelID, true);
    measure("same_normal_map", "已烘焙", panelID, false);
    normal = {128, 128, 255, 255};
    relief->NormalMap->SetData(&normal, sizeof(normal));
    measure("replace_normal_pixels", "已烘焙", panelID, true);
    model.OverrideMaterials.clear();
    measure("remove_normal_map", "已烘焙", panelID, true);

    // 只给当前实体换一张纯蓝底图。取消覆盖后，模型会重新使用 glTF 内的四色贴图。
    const fs::path blue = work / "blue.ppm";
    const char bluePixel[] = "P6\n1 1\n255\n\0\0\xff";
    std::ofstream(blue, std::ios::binary).write(bluePixel, sizeof(bluePixel) - 1);
    Ref<Material> blueMaterial = model.ModelAsset->GetSubMeshes()[0].Mat->Clone();
    blueMaterial->BaseColorMap = Texture2D::Create(blue.string());
    Check(blueMaterial->BaseColorMap->IsLoaded(), "Could not load the test texture");
    model.OverrideMaterials = {blueMaterial};
    glm::u8vec3 changed = measure("override_blue", "实时更新", panelID, true);
    Check(changed.b > changed.r && changed.r == 0 && changed != original, "Blue override did not reach the surface");
    model.OverrideMaterials.clear();
    Check(measure("restore_original", "已烘焙", panelID, true) == original, "Original surface color did not return");

    scene.DestroyEntity(light);
    measure("remove_last_light", "实时更新", panelID, true);
    light = addLight();
    measure("restore_light", "已烘焙", panelID, true);
    scene.DestroyEntity(panel);
    // 编辑器一帧里先画设置面板，再画场景。这个时候输入的烘焙请求须随旧模型一起取消。
    PanelText(preview, work / "shortcut.txt", true);
    glm::u8vec3 background = measure("remove_last_model", "等待场景模型", -1, false);
    Check(background.r == background.g && background.g == background.b, "Expected the empty viewport background");
    Entity cube = scene.CreateEntity("replacement cube");
    cube.AddComponent<MeshRendererComponent>().MeshData = Mesh::CreateCube();
    measure("new_model", "实时更新", int(static_cast<entt::entity>(cube)), true);
    scene.DestroyEntity(cube);
    preview.Load(work / "empty.redflag");
    preview.Mode = Shading::DDGI;
    measure("open_empty_scene", "等待场景模型", -1, false);
    context.WaitIdle();
}

int main(int argc, char** argv)
{
    if (argc != 3) return 1;
    const fs::path work = fs::absolute(argv[1]), evidence = fs::absolute(argv[2]);
    fs::create_directories(work);
    fs::create_directories(evidence.parent_path());
    Log::Init();
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)> window(glfwCreateWindow(640, 480, "Lighting edits", nullptr, nullptr), glfwDestroyWindow);
    if (!window) return 1;
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    try
    {
        VulkanContext context(window.get()); context.Init();
        VulkanImGui gui(context);
        VulkanDraw draw(context);
        draw.Resize(ViewportSize, ViewportSize);
        RendererAPI::SetAPI(RendererAPI::API::Vulkan);
        RenderDevice::SetCurrent(&draw);
        std::ostringstream report;
        CheckScene(context, draw, work, report);
        RenderDevice::SetCurrent(nullptr);
        std::ofstream file(evidence);
        file.exceptions(std::ios::failbit | std::ios::badbit);
        file << report.str();
        file.close();
        std::cout << report.str();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    ImGui::DestroyContext();
    return 0;
}
