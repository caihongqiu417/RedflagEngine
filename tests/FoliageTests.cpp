#include "rfpch.h"
#include "../RedflagForge/src/LightingPreview.h"
#include "../Redflag/vendor/stb_image/stb_image_write.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Platform/Vulkan/VulkanDraw.h"
#include "Redflag/Assets/AssetManager.h"
#include "Redflag/Core/Application.h"
#include "Redflag/Renderer/RendererAPI.h"
#include "Redflag/Renderer/SceneRenderer.h"
#include "Redflag/Scene/Entity.h"
#include "Redflag/Scene/SceneSerializer.h"
#include "RenderDocCapture.h"

using namespace Redflag;
using Pixels = std::vector<glm::u8vec4>;
constexpr int Width = 960, Height = 540;

static void Check(bool value, const char* text)
{
    if (!value) throw std::runtime_error(text);
}

// 两种方式使用同一份草的位置和朝向。对照组仅把实例展开为单次绘制。
class SeparateDraws final : public CommandList
{
public:
    explicit SeparateDraws(VulkanDraw& device) : Target(device) {}
    void DrawIndexed(const Ref<MeshGeometry>& mesh, const Ref<SampledTexture>& texture, const glm::mat4& camera,
        const glm::mat4& world, const glm::vec4& color, int entity, const SurfaceMaterial& surface,
        const std::vector<glm::mat4>* instances) override
    {
        if (instances)
            for (const glm::mat4& instance : *instances) Target.DrawIndexed(mesh,texture,camera,world*instance,color,entity,surface);
        else Target.DrawIndexed(mesh,texture,camera,world,color,entity,surface);
    }
    VulkanDraw& Target;
};

static void Save(const std::filesystem::path& file, const Pixels& image)
{
    Check(stbi_write_png(file.u8string().c_str(),Width,Height,4,image.data(),Width*4) != 0,"Write PNG");
}

int main(int argc, char** argv) try
{
    const std::filesystem::path out = "../output/validation/foliage";
    std::filesystem::create_directories(out);
    const bool separate = argc > 2 && std::string(argv[2]) == "separate";
    RenderDocCapture capture(argc > 3 ? argv[3] : nullptr,
        "../output/captures/local/foliage/" + std::string(separate ? "separate" : "instanced"),
        "Dry grass: 36 plants, same pose and camera","Actual native Vulkan draw; 960x540 color + entity ID. Ground drawn once.");

    Log::Init();
    RendererAPI::SetAPI(RendererAPI::API::Vulkan);
    Application app("Wind meadow",false);
    auto& context = static_cast<VulkanContext&>(app.GetWindow().GetGraphicsContext());
    VulkanDraw device(context);
    RenderDevice::SetCurrent(&device);
    device.Resize(Width,Height);
    SeparateDraws expanded(device);

    const std::string path = "assets/scenes/Wind-Meadow.redflag";
    Ref<Scene> scene = CreateRef<Scene>();
    scene->OnViewportResize(Width,Height);
    Check(SceneSerializer(scene).Deserialize(path),"Load meadow");
    Entity meadow = scene->GetEntityByUUID(102);
    FoliageComponent& plants = meadow.GetComponent<FoliageComponent>();
    Foliage* profile = plants.Get();
    Check(profile && profile->Count == 36,"Load real foliage asset");
    Foliage raised = *profile;
    raised.Root = .37f;
    auto still = raised.Place(0,false), wind = raised.Place(.45,true);
    float rootDrift=0.f, tipMotion=0.f;
    for (size_t i=0; i<still.size(); ++i)
    {
        const glm::vec4 root(0,raised.Root,0,1), tip(0,raised.Root+1.f,0,1);
        rootDrift = std::max(rootDrift,glm::distance(still[i]*root,wind[i]*root));
        tipMotion = std::max(tipMotion,glm::distance(still[i]*tip,wind[i]*tip));
    }
    Check(rootDrift < .00001f && tipMotion > .05f,"Anchored roots and moving tips");
    Check(still == raised.Place(4.,false),"Frozen seed and wind switch");
    raised.Seed++;
    Check(still != raised.Place(0,false),"Different seed moves plants");

    // 编辑器按下运行时复制场景。运行副本上的风开关和已读取的配置能独立修改。
    {
        Ref<Scene> copy = Scene::Copy(scene);
        FoliageComponent& other = copy->GetEntityByUUID(102).GetComponent<FoliageComponent>();
        other.Wind = false;
        other.Visible = false;
        other.Get()->Count = 0;
        Check(plants.Wind && plants.Visible && profile->Count == 36,"Game changes do not alter editor settings");

        const std::filesystem::path temporary = "../tmp/foliage-roundtrip.redflag";
        SceneSerializer(copy).Serialize(temporary.string());
        Ref<Scene> reopened = CreateRef<Scene>();
        reopened->OnViewportResize(Width,Height);
        Check(SceneSerializer(reopened).Deserialize(temporary.string()),"Read saved settings");
        FoliageComponent& saved = reopened->GetEntityByUUID(102).GetComponent<FoliageComponent>();
        Check(!saved.Wind && !saved.Visible && saved.Profile == plants.Profile, "Save and reopen foliage switches");
        auto changedProfile = *profile;
        changedProfile.Lean = .39f;
        std::string error;
        auto file = std::filesystem::path("../tmp/foliage-roundtrip.yaml");
        Check(profile->Save(file,error) && changedProfile.Save(file,error),"Replace existing plant preset");
        Foliage loaded;
        Check(loaded.Load(file,error) && loaded.Lean == .39f,"Saved wind amplitude");
    }

    Entity eye = scene->GetPrimaryCameraEntity();
    const glm::mat4 projection = eye.GetComponent<CameraComponent>().Camera.GetProjection();
    const glm::mat4 view = glm::inverse(eye.GetComponent<TransformComponent>().GetTransform());
    const glm::vec3 origin = eye.GetComponent<TransformComponent>().Translation;
    const glm::mat4 vp = projection*view;
    std::vector<double> cpu[2];
    auto draw = [&](bool expandedDraw, bool record, bool measure = false) {
        if (record) capture.Begin();
        Check(context.BeginFrame(),"Begin real Vulkan frame");
        const auto start = std::chrono::steady_clock::now();
        device.BeginScene();
        CommandList& target = expandedDraw ? static_cast<CommandList&>(expanded) : static_cast<CommandList&>(device);
        SceneRenderer::DrawGeometry(*scene,target,vp);
        device.EndScene();
        if (measure) cpu[expandedDraw].push_back(std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count());
        context.SwapBuffers();
        if (record) capture.End();
    };
    auto ids = [&]() {
        Pixels colors(Width*Height);
        std::vector<int> result(Width*Height);
        device.ReadRegion(0,0,Width,Height,colors.data(),result.data());
        return result;
    };
    plants.Seconds = .45;
    draw(separate,false); // 先上传模型和贴图，截帧只记录已加载模型的绘制。
    draw(separate,true);
    const auto firstIDs = ids();
    draw(!separate,false);
    const auto otherIDs = ids();
    size_t changed = 0;
    for (size_t i=0;i<firstIDs.size();++i) changed += firstIDs[i] != otherIDs[i];
    const size_t covered = std::count(firstIDs.begin(),firstIDs.end(),int(uint32_t(meadow)));
    Check(covered>500 && changed<=12,"Moving grass has equal instanced and separate picking silhouettes");

    if (argc <= 3)
    {
        for (int batch : {0,1,1,0})
        {
            draw(batch,false);
            for (int i=0;i<20;++i) draw(batch,false,true);
        }
        std::ofstream times(out / "record-cpu-us.csv");
        times << "instanced_us,separate_us\n";
        for (size_t i=0;i<cpu[0].size();++i) times << cpu[0][i] << ',' << cpu[1][i] << '\n';
        LightingPreview preview(context,device.GetSampler());
        preview.Load(path);
        auto image = [&](const char* name) {
            Check(preview.Render(*scene,projection,view,origin,Width,Height,false,false)!=VK_NULL_HANDLE,"Render Lit frame");
            Pixels pixels = preview.ReadDisplayPixels();
            Save(out / name,pixels);
            return pixels;
        };
        plants.Wind=false;
        Pixels a = image("calm.png");
        plants.Wind=true;
        plants.Seconds = .45;
        Pixels b = image("wind-045.png");
        plants.Seconds = .90;
        Pixels c = image("wind-090.png");
        plants.Visible=false;
        image("hidden.png");
        draw(false,false);
        auto hidden = ids();
        Check(std::count(hidden.begin(),hidden.end(),int(uint32_t(meadow)))==0,"Hidden plants absent from picking");
        size_t ab=0, bc=0;
        for (size_t i=0;i<a.size();++i) { ab += a[i]!=b[i]; bc += b[i]!=c[i]; }
        Check(ab>500 && bc>500,"Actual Forge lit buffer moves with a fixed camera and disabled TAA");
        std::ofstream metrics(out / "pixels.txt");
        metrics << "resolution 960x540\nplants 36\nmax_root_displacement_m " << rootDrift
            << "\nmax_tip_displacement_m_at_one_model_m " << tipMotion << "\nplant_id_pixels " << covered
            << "\ninstanced_vs_separate_id_mismatch " << changed << "\ncalm_vs_045_changed_RGBA8 " << ab
            << "\n045_vs_090_changed_RGBA8 " << bc << '\n';
    }
    context.WaitIdle();
    scene.reset();
    AssetManager::ClearAll();
    RenderDevice::SetCurrent(nullptr);
    std::cout << "PASS foliage: ID coverage " << covered << ", separate mismatches " << changed << '\n';
    return 0;
}
catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
