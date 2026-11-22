#include "rfpch.h"
#include "Redflag/AI/NpcSystem.h"
#include "Redflag/AI/EnvironmentSense.h"
#include "Redflag/Core/Log.h"
#include "Redflag/Scene/Entity.h"
#include "Redflag/Scene/SceneSerializer.h"
#include <iostream>
#include <thread>
#include <yaml-cpp/yaml.h>

using namespace Redflag;

static void Check(bool pass, const char* message)
{
    if (!pass) throw std::runtime_error(message);
    std::cout << "PASS " << message << '\n';
}

static Ref<Scene> CreateRoom()
{
    Ref<Scene> room = CreateRef<Scene>();
    Entity actor = room->CreateEntityWithUUID(81, "巡检员");
    actor.GetComponent<TransformComponent>().Translation = {8.5f, 0.f, 5.7f};
    actor.AddComponent<NpcComponent>().Definition = "surveyor.npc.yaml";
    actor.GetComponent<NpcComponent>().Leader = 84;
    actor.AddComponent<ScriptComponent>().ClassName = "Workshop.EnvironmentProbe";
    room->CreateEntityWithUUID(84, "队友").GetComponent<TransformComponent>().Translation = {9.f, 0.f, 5.7f};

    Entity field = room->CreateEntityWithUUID(90, "废弃栈桥");
    TransformComponent& transform = field.GetComponent<TransformComponent>();
    transform.Translation = {8.f, 0.f, 4.f};
    transform.Rotation.y = glm::radians(90.f);
    transform.Scale = {-2.f, 1.f, .5f};
    EnvironmentZoneComponent& zone = field.AddComponent<EnvironmentZoneComponent>();
    zone.HalfSize = {1.f, 2.f, 3.f};
    zone.Landform = "河谷阶地";
    zone.Vegetation = "芦苇";
    zone.Landmark = "旧泵房";
    zone.Lights = {91, 92, 84, 999}; // 后两项分别是普通实体和已经不存在的灯具。
    Entity lamp = room->CreateEntityWithUUID(91, "栈桥入口灯");
    lamp.GetComponent<TransformComponent>().Translation = {8.5f, 2.f, 5.7f};
    PointLightComponent& point = lamp.AddComponent<PointLightComponent>();
    point.Color = {1.f, .4f, .2f};
    point.Intensity = 3.f;
    point.Radius = 8.f;
    room->CreateEntityWithUUID(92, "日光").AddComponent<DirectionalLightComponent>();
    room->CreateEntityWithUUID(93, "整个河谷").AddComponent<EnvironmentZoneComponent>().HalfSize = glm::vec3(50.f);
    return room;
}

static void Ask(Scene& room, const char* stage)
{
    NpcSystem& actors = *room.GetNpcs();
    Check(actors.Submit(81, stage), "submitted scene observation");
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (actors.Inspect(81)->Request && std::chrono::steady_clock::now() < deadline)
    {
        room.UpdateGame(1.f / 60.f);
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
    Check(actors.Inspect(81)->Trace.Action == "follow", "gateway response used by scene");
}

// ---- 读取维修站场景副本中的实际区域与灯具 ----
static int SurveyStation()
{
    Ref<Scene> station = CreateRef<Scene>();
    if (!SceneSerializer(station).Deserialize("station-zones.redflag")) return 1;
    Entity inspector = station->CreateEntityWithUUID(81, "巡检员");
    YAML::Node inspections;
    auto record = [&](const std::string& name) {
        YAML::Node facts(YAML::NodeType::Map);
        for (const DecisionFact& fact : SenseEnvironment(*station, inspector.GetUUID()))
            facts[fact.ID] = fact.Description;
        inspections[name] = facts;
    };
    auto zones = station->GetRegistry().view<EnvironmentZoneComponent, TransformComponent>();
    for (entt::entity handle : zones)
    {
        Entity area(handle, station.get());
        inspector.GetComponent<TransformComponent>().Translation = area.GetComponent<TransformComponent>().Translation;
        const std::string name = std::to_string(area.GetUUID());
        record(name);
        for (uint64_t id : area.GetComponent<EnvironmentZoneComponent>().Lights)
        {
            Entity light = station->GetEntityByUUID(id);
            if (!light || !light.HasComponent<PointLightComponent>()) continue;
            float& intensity = light.GetComponent<PointLightComponent>().Intensity;
            const float previous = intensity;
            intensity = 0.f;
            record(name + "-off-" + std::to_string(id));
            intensity = previous;
        }
    }
    inspector.GetComponent<TransformComponent>().Translation = glm::vec3(10000.f);
    record("outside");
    std::ofstream("sensed.yaml") << YAML::Dump(inspections);
    return 0;
}

int main(int argc, char** argv)
{
    if (argc != 4) return 1;
    std::filesystem::current_path(std::filesystem::u8path(argv[3]));
    Log::Init();
    if (std::string(argv[2]) == "station") return SurveyStation();
    ScriptEngine::Init();
    try
    {
        Ref<Scene> author = CreateRoom(), room = CreateRef<Scene>();
        SceneSerializer(author).Serialize("marsh.redflag");
        Check(SceneSerializer(room).Deserialize("marsh.redflag"), "reopened authored scene");
        Ref<Scene> play = Scene::Copy(room);
        Check(SenseEnvironment(*play, 81).size() == 7, "two overlapping zones and two actual lights");
        room->DuplicateEntity(room->GetEntityByUUID(90));
        Check(SenseEnvironment(*room, 81).size() == 13 && SenseEnvironment(*play, 81).size() == 7,
            "duplicate keeps zone settings and the play scene stays independent");
        play->OnRuntimeStart(static_cast<uint16_t>(std::stoi(argv[2])));
        Ask(*play, "inside");
        play->GetEntityByUUID(91).GetComponent<PointLightComponent>().Intensity = 0.f;
        Ask(*play, "lamp-off");
        play->DestroyEntity(play->GetEntityByUUID(91));
        Ask(*play, "lamp-removed");
        play->GetEntityByUUID(81).GetComponent<TransformComponent>().Translation.z = 6.1f;
        Ask(*play, "outside-inner");
        play->GetEntityByUUID(81).GetComponent<TransformComponent>().Translation.z = 55.f;
        Ask(*play, "outside-all");
        play->GetEntityByUUID(81).GetComponent<TransformComponent>().Translation.z = 5.7f;
        play->GetNpcs()->GetDefinition(81)->ObserveWorld = false;
        Ask(*play, "disabled");
        play->OnRuntimeStop();
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
    ScriptEngine::Shutdown();
}
