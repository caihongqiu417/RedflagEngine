#include "rfpch.h"
#include "../RedflagForge/src/NpcCamera.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Redflag/AI/NpcSystem.h"
#include "Redflag/AI/PhotoQuestion.h"
#include "Redflag/Core/Application.h"
#include "Redflag/Renderer/RendererAPI.h"
#include "Redflag/Scene/Entity.h"
#include "Redflag/Scene/SceneSerializer.h"
#include <thread>
#include <iomanip>

using namespace Redflag;

static void Require(bool passed,const char* what)
{
    if (!passed) throw std::runtime_error(what);
    std::cout << "PASS " << what << '\n';
}

static Entity Box(Scene& scene, uint64_t id, glm::vec3 position, glm::vec3 scale, glm::vec3 color)
{
    Entity entity = scene.CreateEntityWithUUID(id, "Object");
    auto& transform = entity.GetComponent<TransformComponent>();
    transform.Translation = position;
    transform.Scale = scale;
    auto& surface = entity.AddComponent<MeshRendererComponent>();
    surface.MeshData = Mesh::CreateCube();
    surface.Color = glm::vec4(color,1);
    surface.Mat = Material::Create(surface.Color);
    return entity;
}

int main(int argc,char** argv) try
{
    if (argc < 3) throw std::runtime_error("Usage: --npc-photo <output> [model port]");
    const std::filesystem::path out = argv[2];
    const uint16_t port = argc > 3 ? uint16_t(std::stoi(argv[3])) : 8716;
    std::filesystem::create_directories(out);
    Log::Init();
    RendererAPI::SetAPI(RendererAPI::API::Vulkan);
    Application app("NPC camera", false);
    auto& context = static_cast<VulkanContext&>(app.GetWindow().GetGraphicsContext());
    VulkanDraw device(context);
    RenderDevice::SetCurrent(&device);
    NpcCamera camera(context);
    auto room = CreateRef<Scene>();
    Entity observer = room->CreateEntityWithUUID(42, "Observer");
    observer.AddComponent<NpcComponent>().Definition = "../examples/npc-runtime/technician.npc.yaml";
    // White wall fills the frame; the offscreen point light is the only light source.
    Box(*room, 60, {0,1.5f,4}, {5,3,.3f}, {1,1,1});
    Entity object = Box(*room, 61, {-.7f,1.55f,2.5f}, {.55f,.55f,.55f}, {1,.003f,.003f});
    object.AddComponent<PickupComponent>().Item = "sample";
    object.AddComponent<BoxCollider3DComponent>();
    object.AddComponent<RigidBody3DComponent>();
    Entity lamp = room->CreateEntity("Source");
    lamp.GetComponent<TransformComponent>().Translation = {1,2.8f,1.2f};
    auto& light = lamp.AddComponent<PointLightComponent>();
    light.Color = {1,1,1}; light.Intensity = 100; light.Radius = 12;
    room->OnRuntimeStart(port);
    const auto profile = room->GetNpcs()->GetDefinition(42);
    Require(profile != nullptr, "load real NPC definition");
    profile->ObserveWorld = false;
    profile->Memory.Sight = false;
    PhotoQuestion ask(port);
    std::ofstream log(out / "observations.tsv");
    log << "file\tobserver\tcaptured_seconds\tx\ty\tz\tdx\tdy\tdz\tprompt\tanswer\tentity\tworld_receipt\tphoto_id\tremembered\n";
    uint64_t observerID = 42;
    std::filesystem::path scenePath;
    auto query = [&](const char* file, PhotoSubject subject, const char* text, const std::function<void()>& afterPhoto = {}) {
        NpcPhoto photo = camera.Take(*room,observerID,scenePath,subject == PhotoSubject::Object);
        const auto& image = photo.Image;
        std::ofstream png(out/file, std::ios::binary);
        png.write(reinterpret_cast<const char*>(image.Bytes.data()),image.Bytes.size());
        Require(ask.Start(room,std::move(photo),subject,text), "submit live photograph");
        if (afterPhoto) afterPhoto();
        while (ask.Pending()) { ask.Update(); std::this_thread::sleep_for(std::chrono::milliseconds(15)); }
        const NpcPhoto& original = ask.GetPhoto();
        const auto& notes = room->GetNpcs()->GetMemory(observerID)->GetEvidence();
        const bool seen = std::any_of(notes.begin(),notes.end(), [&](const NpcEvidence& e) {
            return e.Source == EvidenceSource::Photo && e.Image == original.Image.ID && e.Seconds == original.Seconds;
        });
        log << file << '\t' << observerID << '\t' << original.Seconds << '\t' << original.Eye.x << '\t' << original.Eye.y << '\t' << original.Eye.z
            << '\t' << original.Forward.x << '\t' << original.Forward.y << '\t' << original.Forward.z << '\t' << text << '\t'
            << ask.Answer() << '\t' << ask.Target() << '\t' << ask.Receipt() << '\t' << original.Image.ID << '\t' << int(seen) << std::endl;
        std::cout << file << " " << ask.Answer() << " " << ask.Target() << " " << ask.Receipt() << "\n";
        return seen;
    };
    const char* brightness = "Is the wall in front of this camera bright and illuminated or very dark? Judge its visible surface.";
    const char* red = "Which candidate region corresponds to the small vivid red cube, if visible? The white wall is not the requested object.";
    const bool bright = query("01.png",PhotoSubject::Lighting,brightness) && ask.Answer()=="lit";
    light.Intensity = 0;
    const bool dark = query("02.png",PhotoSubject::Lighting,brightness) && ask.Answer()=="dark";
    light.Intensity = 100;
    const bool present = query("03.png",PhotoSubject::Object,red) && ask.Target()==61;
    const bool blocked = !query("06.png",PhotoSubject::Object,red,[&] {
        Entity panel = room->CreateEntityWithUUID(62,"Panel");
        panel.GetComponent<TransformComponent>().Translation = {-.4f,1.55f,1.4f};
        panel.AddComponent<RigidBody3DComponent>();
        panel.AddComponent<BoxCollider3DComponent>().HalfSize = {1,1,.12f};
        room->OnUpdatePhysics(1.f/60);
    }) && ask.Target()==61 && ask.Receipt()=="当前位置看不到该物体";
    room->DestroyEntity(room->GetEntityByUUID(62));
    const bool removed = !query("04.png",PhotoSubject::Object,red,[&] { room->DestroyEntity(object); }) && ask.Target()==61 && ask.Receipt()=="照片中的物体已离开场景";
    const bool absent = query("05.png",PhotoSubject::Object,red) && ask.Answer()=="absent";
    profile->ObserveWorld = true;
    profile->Memory.Sight = true;
    profile->Memory.Pictures = false;
    Require(camera.Take(*room,42,{}).Image.Bytes.empty(), "camera disabled independently of engine observations");
    profile->Memory.Pictures = true;
    Require(ask.Start(room,camera.Take(*room,42,{},false),PhotoSubject::Lighting,brightness),"submit before stopping Play");
    room->OnRuntimeStop();
    room->OnRuntimeStart(port);
    while (ask.Pending()) { ask.Update(); std::this_thread::sleep_for(std::chrono::milliseconds(15)); }
    Require(ask.Receipt()=="本次观察已结束" && room->GetNpcs()->GetMemory(42)->GetEvidence().empty(),
        "photo from previous Play session cannot enter the new character memory");
    room->OnRuntimeStop();
    if (argc > 4 && std::string(argv[4]) == "--station")
    {
        room = CreateRef<Scene>();
        room->OnViewportResize(1920,1080);
        scenePath = "assets/scenes/Watt-Station.redflag";
        Require(SceneSerializer(room).Deserialize(scenePath.string()), "open Watt Station from the engine");
        room->OnRuntimeStart(port);
        room->OnUpdateAnimations(1.f/60);
        observerID = 1102;
        Require(query("station.png",PhotoSubject::Description,"请用两三句中文描述眼前可见的地表、设施和光线，看不清的不要推测。"),"station NPC remembered its camera exposure");
        room->OnRuntimeStop();
    }
    Require(bright && dark,"same camera and prompt, light on and off");
    Require(present,"target in photo confirmed in current scene");
    Require(blocked,"new wall between observer and target rejected after model reply");
    Require(removed,"deleted entity rejected after model reply");
    Require(absent,"model sees no matching object after it leaves the photo");
    return 0;
}
catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
