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
    scene->OnSimulationStart();
    Run(*scene, argc > 2 ? static_cast<uint16_t>(std::stoi(argv[2])) : 8716, argc > 3 ? argv[3] : "race");
    scene->OnSimulationStop();
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
