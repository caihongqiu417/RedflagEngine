#include "rfpch.h"
#include "Redflag/AI/NpcSystem.h"
#include "Redflag/Core/Log.h"
#include "Redflag/Scene/Entity.h"
#include <iostream>
#include <thread>

using namespace Redflag;

static void Require(bool pass, const char* message)
{
    if (!pass) throw std::runtime_error(message);
    std::cout << "PASS " << message << '\n';
}

static void Ask(Scene& room, const char* expectedChoice)
{
    NpcSystem& actors = *room.GetNpcs();
    Require(actors.Submit(81, "你现在能确认阀门的颜色吗？"), "request accepted");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (actors.Inspect(81)->Request && std::chrono::steady_clock::now() < deadline)
    {
        room.UpdateGame(1.f / 60.f);
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
    room.UpdateGame(1.f / 60.f);
    Require(!actors.Inspect(81)->Request && actors.Inspect(81)->Trace.Edges.size() == 1, "role followed a connected tree edge");
    const auto& events = actors.Inspect(81)->Events;
    auto picked = std::find_if(events.rbegin(), events.rend(), [](const NpcEvent& e) { return e.Kind == "选择"; });
    Require(picked != events.rend() && picked->Text.find(expectedChoice) == 0, expectedChoice);
}

static void RunRoom(uint16_t port, bool reopen)
{
    Scene room;
    Entity surveyor = room.CreateEntityWithUUID(81, "巡检员");
    NpcComponent& role = surveyor.AddComponent<NpcComponent>();
    role.Definition = "surveyor.npc.yaml";
    role.Leader = 84;
    surveyor.AddComponent<ScriptComponent>().ClassName = "Workshop.SurveyNotes";
    Entity valve;
    if (!reopen)
    {
        valve = room.CreateEntityWithUUID(82, "红色阀门");
        valve.GetComponent<TransformComponent>().Translation.z = 4.f;
        valve.AddComponent<PickupComponent>().Item = "valve";
    }
    room.CreateEntityWithUUID(84, "队友").GetComponent<TransformComponent>().Translation.x = -1.f;
    room.OnRuntimeStart(port); // 本局的 C# OnCreate 在这里写转述或读取偏好存档。
    if (reopen)
        Ask(room, "revisit");
    else
    {
        Ask(room, "confirm");
        Entity wall = room.CreateEntity("隔板");
        wall.GetComponent<TransformComponent>().Translation.z = 2.f;
        wall.AddComponent<RigidBody3DComponent>();
        wall.AddComponent<BoxCollider3DComponent>().HalfSize = {1.f, 1.f, .1f};
        room.UpdateGame(1.f / 60.f);
        Require(!CanReach(room, 81, 82, 6.f), "PhysX panel blocks the valve");
        Ask(room, "revisit");
        room.DestroyEntity(valve);
        Ask(room, "revisit");
    }
    room.OnRuntimeStop();
}

static void CheckPolicy()
{
    NpcMemory notes;
    MemoryPolicy policy;
    policy.Limit = 2;
    policy.Preferences = {{"route", PreferenceScope::Session}, {"language", PreferenceScope::Saved}};
    notes.Configure(policy);
    notes.Report(82, "玩家的描述", 1.);
    notes.See(82, "阀门", 2.);
    notes.BeginSight();
    Require(notes.GetEvidence().back().Source == EvidenceSource::LastSeen && notes.GetEvidence().back().Seconds == 2.,
        "last sight keeps original time and remains separate from report");
    notes.Report(0, "无实体编号的照片", 3., "panel.png");
    Require(notes.GetEvidence().size() == 2 && notes.GetEvidence().front().Source == EvidenceSource::LastSeen,
        "record budget drops the oldest entry first");
    notes.SetPreference("route", "north");
    notes.SetPreference("language", "zh");
    Require(!notes.LoadPreferences("language: [broken]") && *notes.GetPreference("language") == "zh",
        "invalid save preserves preferences");
    Require(notes.LoadPreferences("language: en\nunknown: value") && *notes.GetPreference("route") == "north" &&
        !notes.GetPreference("unknown") && *notes.GetPreference("language") == "en", "restore only allowed saved keys");

    policy.KeepSight = false;
    policy.Reports = false;
    policy.Preferences.clear();
    notes.Configure(policy);
    Require(notes.GetEvidence().empty() && !notes.GetPreference("route") && !notes.Report(82, "玩家的描述", 4.),
        "changed role settings discard forbidden records and preferences");
    notes.See(82, "阀门", 5.);
    notes.BeginSight();
    Require(notes.GetEvidence().empty(), "role without sight history forgets the previous view");
}

int main(int argc, char** argv)
{
    if (argc != 4) return 1;
    std::filesystem::current_path(std::filesystem::u8path(argv[3]));
    Log::Init();
    ScriptEngine::Init();
    try
    {
        const uint16_t port = static_cast<uint16_t>(std::stoi(argv[2]));
        CheckPolicy();
        RunRoom(port, false);
        RunRoom(port, true);
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
    ScriptEngine::Shutdown();
}
