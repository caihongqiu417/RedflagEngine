#include "rfpch.h"
#include "../RedflagForge/src/NpcCamera.h"
#include "../RedflagForge/src/NpcPhotoPanel.h"
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

// 队友只给大致方位，场景中甚至没有补给实体。走近后用与正式游戏相同的相机接入再次提问。
static void CheckSearch(VulkanContext& context, uint16_t port, const std::filesystem::path& out)
{
    Ref<Scene> room = CreateRef<Scene>();
    Entity actor = room->CreateEntityWithUUID(42, "瓦特");
    actor.AddComponent<NpcComponent>().Definition = "assets/ai/watt.npc.yaml";
    actor.AddComponent<InventoryComponent>();
    Box(*room, 62, {0,1.2f,8}, {3,3,.4f}, {.6f,.45f,.2f});
    Entity light = room->CreateEntity("Light");
    light.AddComponent<PointLightComponent>().Intensity = 30;
    light.GetComponent<TransformComponent>().Translation = {0,2,4};
    room->OnRuntimeStart(port);
    NpcSystem& npc = *room->GetNpcs();
    npc.GetDefinition(42)->ObserveWorld = npc.GetDefinition(42)->Memory.Sight = false;
    NpcPhotoPanel photo;
    photo.Connect(context, room, {});
    const std::string report = "前面似乎有一箱补给，也可能只是个空架子。";
    Require(npc.GetMemory(42)->Report(0, report, npc.SessionSeconds(), {}, glm::vec3(0,0,4)), "accept a location without an entity");
    std::ofstream log(out / "search.tsv");
    log << "seconds\tkind\ttext\tentity\tz\tspeech\n";
    uint64_t cursor = 0;
    auto until = [&](const std::string& kind, bool readCamera = true) {
        const auto untilTime = std::chrono::steady_clock::now() + std::chrono::seconds(90);
        do {
            room->UpdateGame(1.f/60);
            if (readCamera) photo.Update();
            const NpcState* state = npc.Inspect(42);
            while (const NpcEvent* event = npc.NextEvent(42, cursor)) {
                log << event->Time << '\t' << event->Kind << '\t' << event->Text << '\t' << event->Target << '\t'
                    << actor.GetComponent<TransformComponent>().Translation.z << '\t' << event->Speech << std::endl;
                if (event->Kind == kind) return true;
            }
            if (!readCamera && state->Look) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        } while (std::chrono::steady_clock::now() < untilTime);
        return false;
    };

    Require(npc.Submit(42,"先别动，把队友刚才那条没核实的消息转告我。") && until("报点"), "relay an uncertain report");
    Require(actor.GetComponent<TransformComponent>().Translation.z == 0, "relaying a clue does not move the character");
    Require(npc.Submit(42,"去我标记的地方，走近一点重新观察。") && until("复看"), "walk to the suggested area and look again");
    const auto& notes = npc.GetMemory(42)->GetEvidence();
    Require(notes.front().Visited && !notes.front().Target && notes.back().Source == EvidenceSource::Photo && !notes.back().Image.empty(),
        "the actor remembers the original report and a new visual judgment");
    Require(actor.GetComponent<TransformComponent>().Translation.z >= 2.9f && actor.GetComponent<InventoryComponent>().Items.empty(),
        "walking and observing never grant inventory");
    log << "photo\t" << notes.back().Image << '\t' << notes.back().Text << std::endl;

    Require(npc.Submit(42,"原地看一眼周围，告诉我你眼前有什么。") && until("waiting for camera", false), "begin another look without any object");
    const uint64_t old = npc.Inspect(42)->Look;
    const size_t count = notes.size();
    Require(npc.Cancel(42), "interrupt an ongoing look");
    photo.Update();
    npc.FinishLook(42,old,"过期的照片答复");
    Require(!npc.Inspect(42)->Look && !npc.Inspect(42)->Request && notes.size() == count, "late vision cannot restart the cancelled action");
    photo.Stop();
    room->OnRuntimeStop();
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
    if (argc > 4 && std::string(argv[4]) == "--clue")
    {
        CheckSearch(context,port,out);
        return 0;
    }
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
    const bool blocked = query("06.png",PhotoSubject::Object,red,[&] {
        Entity panel = room->CreateEntityWithUUID(62,"Panel");
        panel.GetComponent<TransformComponent>().Translation = {-.4f,1.55f,1.4f};
        panel.AddComponent<RigidBody3DComponent>();
        panel.AddComponent<BoxCollider3DComponent>().HalfSize = {1,1,.12f};
        room->OnUpdatePhysics(1.f/60);
    }) && ask.Target()==61 && !CanReach(*room,42,61,8.f);
    room->DestroyEntity(room->GetEntityByUUID(62));
    const bool removed = query("04.png",PhotoSubject::Object,red,[&] { room->DestroyEntity(object); }) && ask.Target()==61 &&
        CollectItem(*room,42,61,8.f) != ActionResult::Completed;
    const bool absent = query("05.png",PhotoSubject::Object,red) && (ask.Answer()=="absent" || ask.Answer()=="uncertain");
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
    Require(present,"photograph identifies the earlier red object");
    Require(blocked,"blocked object remains a photo clue but is out of reach");
    Require(removed,"old photograph remains a clue after removal and cannot award an item");
    Require(absent,"empty photograph may return absent or uncertain");
    return 0;
}
catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
