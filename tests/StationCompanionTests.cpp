#include "rfpch.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Platform/Vulkan/VulkanDraw.h"
#include "Redflag/AI/NpcSystem.h"
#include "Redflag/Assets/AssetManager.h"
#include "Redflag/Core/Application.h"
#include "Redflag/Gameplay/TerrainTraversal.h"
#include "Redflag/Renderer/RendererAPI.h"
#include "Redflag/Scene/Entity.h"
#include "Redflag/Scene/SceneSerializer.h"
#include <thread>

using namespace Redflag;

static void Check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
    std::cout << "PASS " << message << std::endl;
}

static void Watch(Scene& scene, uint16_t port, const std::string& scenario)
{
    Entity guard = scene.GetEntityByUUID(1103), player = scene.GetEntityByUUID(1101), watt = scene.GetEntityByUUID(1102);
    auto& pose = watt.GetComponent<TransformComponent>();
    const glm::vec3 machine = guard.GetComponent<TransformComponent>().Translation;
    // 用同一个存档摆三个人的起点。放电动作和实际扣血都交给正式 StationGuard 脚本。
    player.GetComponent<TransformComponent>().Translation = machine + glm::vec3(-1.50f, 0.f, .95f);
    pose.Translation = machine + glm::vec3(-.60f, 0.f, 2.35f);
    pose.Rotation.y = std::atan2(.60f, -2.35f);
    if (scenario == "watch-back") pose.Rotation.y += glm::pi<float>();
    if (scenario == "watch-wall")
    {
        Entity wall = scene.CreateEntity("遮挡观察的隔板");
        wall.GetComponent<TransformComponent>().Translation = machine + glm::vec3(-.30f, 1.2f, 1.18f);
        wall.AddComponent<RigidBody3DComponent>();
        wall.AddComponent<BoxCollider3DComponent>().HalfSize = {.42f, 1.4f, .12f};
    }
    scene.OnRuntimeStart(port);
    NpcSystem& npcs = *scene.GetNpcs();
    if (scenario == "watch-quiet")
        Check(npcs.GetMemory(1102)->SetPreference("提醒方式", "不用电台说话，看见任何情况都保持安静，只记在记录里。"),
            "set the listener's session preference");

    const float before = player.GetComponent<HealthComponent>().Current;
    int seen = 0, spoken = 0, quiet = 0, withdrawn = 0;
    bool active = false;
    uint64_t cursor = 0;
    for (int frame = 0; frame < 200; ++frame)
    {
        scene.UpdateGame(1.f / 60.f);
        active |= guard.GetComponent<ModelAnimationComponent>().Clip == "Cannon_Fire_Recoil";
        while (const NpcEvent* event = npcs.NextEvent(1102, cursor))
        {
            if (event->Target != 1103) continue;
            std::cout << "WATCH\t" << frame << '\t' << event->Kind << '\t' << event->Text << '\t'
                      << event->Target << '\t' << event->Speech << std::endl;
            seen += event->Kind == "主动观察";
            spoken += event->Kind == "主动报点" && !event->Speech.empty();
            quiet += event->Kind == "静默观察" && event->Speech.empty();
            withdrawn += event->Kind == "观察结束" && event->Speech.empty();
            if (scenario == "watch-lost" && event->Kind == "主动观察")
                player.GetComponent<TransformComponent>().Translation = {-6.9f, .08f, 1.4f};
        }
        // 对局按固定步长继续；留出真实墙钟时间给本机推理服务，不让高速测试先走完几分钟游戏。
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
        if (frame >= 48 && (spoken || quiet || withdrawn)) break;
        if (frame >= 48 && (scenario == "watch-wall" || scenario == "watch-back")) break;
    }
    const float after = player.GetComponent<HealthComponent>().Current;
    std::cout << "WORLD\t" << scenario << "\thealth=" << before << "->" << after << "\tseen=" << seen
              << "\tspoken=" << spoken << "\tquiet=" << quiet << "\twithdrawn=" << withdrawn
              << "\taction=" << npcs.Inspect(1102)->Action << std::endl;
    Check(active && after < before, "actual guard script played its cue and damaged the player");
    Check(npcs.Inspect(1102)->Action == "follow", "watching does not interrupt the current action");
    if (scenario == "watch-wall" || scenario == "watch-back")
        Check(!seen && !spoken && !quiet, "an occluded or out-of-view performance was not observed");
    else if (scenario == "watch-lost")
        Check(seen == 1 && withdrawn == 1 && !spoken && !quiet, "discarded the observation after the clip ended");
    else
        Check(seen == 1 && (scenario == "watch-quiet" ? quiet == 1 && !spoken : spoken == 1 && !quiet),
              "the same sustained display produced one decision matching the listener preference");
    scene.OnRuntimeStop();
}

static void Run(Scene& scene, uint16_t port, const std::string& scenario)
{
    constexpr uint64_t robot = 1102, player = 1101, supply = 1112;
    constexpr float step = 1.f / 60.f;
    NpcSystem actors(scene, port);
    uint64_t cursor = 0;
    auto point = [&](uint64_t id) { return scene.GetEntityByUUID(id).GetComponent<TransformComponent>().Translation; };
    auto inventory = [&](uint64_t id) {
        const auto& stock = scene.GetEntityByUUID(id).GetComponent<InventoryComponent>().Items;
        const auto item = stock.find("medkit");
        return item == stock.end() ? 0 : item->second;
    };
    auto health = [&]() { return scene.GetEntityByUUID(player).GetComponent<HealthComponent>().Current; };
    auto tick = [&]() {
        actors.Update(step);
        scene.OnUpdatePhysics(step);
        while (const NpcEvent* event = actors.NextEvent(robot, cursor))
            std::cout << "EVENT\t" << event->Sequence << '\t' << event->Kind << '\t' << event->Text << '\t' << event->Target
                      << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    };
    auto until = [&](const char* title, const std::function<bool(const NpcState&)>& done) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        do
        {
            tick();
            if (done(*actors.Inspect(robot)))
            {
                Check(true, title);
                return;
            }
        } while (std::chrono::steady_clock::now() < end);
        const auto& state = *actors.Inspect(robot);
        std::cerr << "Timeout " << title << ": action=" << state.Action << ", stage=" << state.Stage
                  << ", request=" << state.Request << std::endl;
        Check(false, title);
    };

    // 使用正式场景及角色配置。测试输入只操纵玩家、发话或触发环境伤害。
    const glm::vec3 start = point(robot);
    until("silent companion walks toward the player",
          [&](const NpcState& s) { return s.Action == "follow" && glm::distance(start, point(robot)) > .1f; });
    Check(actors.Submit(robot, "在这里等我，不要跟过来。"), "submit hold command");
    until("real model picks wait", [](const NpcState& s) { return s.Action == "wait"; });
    const glm::vec3 held = point(robot);
    auto walkPlayer = [&](glm::vec3 destination) {
        for (int frame = 0; frame < 300; ++frame)
        {
            glm::vec3 delta = destination - point(player);
            delta.y = 0;
            if (glm::length(delta) < .1f) break;
            MoveCharacter(scene, player, glm::normalize(delta) * .045f, .35f, step);
            tick();
        }
    };
    if (scenario == "deliver" || scenario == "reserve")
    {
        walkPlayer({-7.5f, .08f, 4.96f});
        const glm::vec3 playerStart = point(player);
        const float separation = glm::distance(playerStart, held);
        const int stock = inventory(robot), bag = inventory(player);
        Check(separation >= 5.f && glm::distance(held, point(robot)) < .001f,
              "start with the characters at least five metres apart");
        const bool deliver = scenario == "deliver";
        const std::string order = deliver ? "请把你背包里的一份医疗包送到我手上。" : "帮我留一份医疗包，等我过来再给我。";
        Check(actors.Submit(robot, order), "send the player's exact order");
        until("model distinguishes delivery from a reservation",
              [&](const NpcState& s) { return s.Action == (deliver ? "share" : "reserve"); });
        if (!deliver)
        {
            for (int i = 0; i < 180; ++i)
                tick();
            Check(glm::distance(held, point(robot)) < .001f && inventory(robot) == stock && inventory(player) == bag,
                  "reservation holds both inventory and position for three seconds");
            glm::vec3 toward = glm::normalize(held - point(player));
            walkPlayer(held - toward * 1.3f);
        }
        until("one pack physically transfers to the player", [&](const NpcState&) { return inventory(player) == bag + 1; });
        const float robotTravel = glm::distance(held, point(robot)),
                    playerTravel = glm::distance(playerStart, point(player));
        std::cout << "DISTANCE\t" << scenario << "\tstart_m=" << separation << "\trobot_m=" << robotTravel
                  << "\tplayer_m=" << playerTravel << "\trobot_stock=" << stock << "->" << inventory(robot)
                  << "\tplayer_stock=" << bag << "->" << inventory(player) << std::endl;
        Check(inventory(robot) == stock - 1 &&
                  (deliver ? robotTravel > 3.f && playerTravel < .001f : robotTravel < .001f && playerTravel > 3.f),
              "only the requested character approached for the handoff");
        return;
    }

    walkPlayer(point(supply) + glm::vec3(-.95f, 0, 1.f));
    Check(glm::distance(point(robot), held) < .001f && CanReach(scene, player, supply, 2.f),
          "wait holds position while player walks to supply");

    Check(actors.Submit(robot, "跟我走。"), "submit follow command");
    until("real model resumes follow", [](const NpcState& s) { return s.Action == "follow"; });
    const auto choices = FindCandidates(scene, robot, *actors.GetDefinition(robot), actors.Inspect(robot)->Memory, false);
    for (const NpcCandidate& choice : choices)
        std::cout << "OFFER\t" << choice.ID << '\t' << choice.Target << std::endl;
    Check(std::any_of(choices.begin(), choices.end(), [](const NpcCandidate& c) { return c.ID == "gather"; }),
          "Watt sees the supply from its real position");
    Check(actors.Submit(robot, "去捡地上的急救工具箱，把它装到你自己的背包里。"), "submit gather command");
    until("the supply subtree asks the model again", [](const NpcState& s) { return s.Request && s.Trace.Waiting == 3; });
    const int before = inventory(robot), bag = inventory(player);
    Check(CollectItem(scene, player, supply, 2.f) == ActionResult::Completed,
          "player physically takes the target before the second reply");
    until("the companion replans from remaining options",
          [](const NpcState& s) { return !s.Request && s.Action == "follow"; });
    Check(!scene.GetEntityByUUID(supply) && inventory(robot) == before && inventory(player) == bag + 1,
          "vanished target never credits a second pickup");

    Check(actors.Submit(robot, "现在请原地待命。"), "submit a new order before rescue");
    Check(ApplyDamage(scene, 0, player, 150.f), "game damage downs the player");
    until("downed player interrupts the outstanding order", [](const NpcState& s) { return s.Action == "revive"; });
    until("timed interaction revives player and then resumes follow",
          [&](const NpcState& s) { return health() > 0 && s.Action == "follow"; });
    Check(health() == 50.f && inventory(robot) == before, "rescue applies the scene-authored 50 health exactly once");

    Check(actors.Submit(robot, "先在这里等我。") && actors.Cancel(robot),
          "explicit cancel stops an outstanding model request");
    const glm::vec3 stopped = point(robot);
    for (int i = 0; i < 120; ++i)
        tick();
    Check(!actors.Inspect(robot)->Request && actors.Inspect(robot)->Action.empty() &&
              glm::distance(stopped, point(robot)) < .001f,
          "cancelled replies cannot restart the old order");
}

int main(int argc, char** argv)
try
{
    Log::Init();
    RendererAPI::SetAPI(RendererAPI::API::Vulkan);
    Application app("Station companion tests", false);
    auto& context = static_cast<VulkanContext&>(app.GetWindow().GetGraphicsContext());
    VulkanDraw device(context);
    RenderDevice::SetCurrent(&device);
    Ref<Scene> scene = CreateRef<Scene>();
    scene->OnViewportResize(640, 400);
    Check(SceneSerializer(scene).Deserialize("assets/scenes/Watt-Station.redflag"), "load actual Watt Station");
    const uint16_t port = argc > 2 ? static_cast<uint16_t>(std::stoi(argv[2])) : 8716;
    const std::string scenario = argc > 3 ? argv[3] : "race";
    if (scenario.find("watch-") == 0) Watch(*scene, port, scenario);
    else
    {
        scene->OnSimulationStart();
        Run(*scene, port, scenario);
        scene->OnSimulationStop();
    }
    scene.reset();
    context.WaitIdle();
    AssetManager::ClearAll();
    RenderDevice::SetCurrent(nullptr);
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << std::endl;
    return 1;
}
