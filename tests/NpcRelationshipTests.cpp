#include "rfpch.h"
#include "Redflag/AI/NpcSystem.h"
#include "Redflag/Core/Log.h"
#include "Redflag/Scene/Entity.h"
#include <thread>

using namespace Redflag;

static bool Run(const std::filesystem::path& profile, uint16_t port, bool stocked)
{
    Scene room;
    Entity actor = room.CreateEntityWithUUID(41, "补给员");
    actor.AddComponent<NpcComponent>() = {profile.u8string(), "", 42};
    actor.AddComponent<HealthComponent>().Team = "crew";
    actor.AddComponent<InventoryComponent>();
    if (stocked) actor.GetComponent<InventoryComponent>().Items["medkit"] = 1;

    Entity player = room.CreateEntityWithUUID(42, "队友");
    player.AddComponent<InventoryComponent>();
    player.AddComponent<HealthComponent>().Team = "crew";
    auto& playerPos = player.GetComponent<TransformComponent>().Translation;
    playerPos.z = 4.f;
    room.OnSimulationStart();

    NpcSystem npcs(room, port);
    const bool submitted = npcs.Submit(41, "按我们的老规矩处理这份医疗包。");
    std::string chosen, first, finished;
    float atDecision = 0.f, beforeArrival = 0.f;
    uint64_t cursor = 0;
    bool waited = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (submitted && finished.empty() && std::chrono::steady_clock::now()<deadline)
    {
        npcs.Update(1.f/60.f);
        while (const NpcEvent* row = npcs.NextEvent(41,cursor))
        {
            const float z = actor.GetComponent<TransformComponent>().Translation.z;
            if (row->Kind == "选择") { chosen = npcs.Inspect(41)->Goal.Intent; atDecision = z; }
            if (row->Kind == "动作" && row->Text != "idle" && first.empty()) first = row->Text;
            if (row->Kind == "动作" && row->Text == "wait")
            {
                // 等待者确实仍在原处，之后才把队友移近，让同一套交接逻辑完成。
                waited = true;
                beforeArrival = z;
                playerPos.z = 1.f;
            }
            if (row->Kind == "库存") beforeArrival = z;
            if (row->Kind == "完成") finished = row->Text;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
    const uint32_t received = player.GetComponent<InventoryComponent>().Items["medkit"];
    std::cout << "ROLE\t" << profile.filename().u8string() << '\t' << submitted << '\t' << chosen << '\t' << first
        << '\t' << finished << '\t' << atDecision << '\t' << beforeArrival << '\t' << received << std::endl;
    room.OnSimulationStop();
    if (!stocked) return !submitted && received == 0;
    if (chosen == "reserve") return waited && first == "wait" && beforeArrival == 0.f && finished == chosen && received == 1;
    return chosen == "share" && first == "walk" && beforeArrival >= 1.9f && finished == chosen && received == 1;
}

int main(int argc, char** argv)
{
    if (argc!=4) return 2;
    Log::Init();
    const auto folder = std::filesystem::u8path(argv[3]);
    const uint16_t port = uint16_t(std::stoi(argv[2]));
    const bool lin = Run(folder / "lin.npc.yaml",port,true);
    const bool shen = Run(folder / "shen.npc.yaml",port,true);
    const bool empty = Run(folder / "shen.npc.yaml",port,false);
    return lin && shen && empty ? 0 : 1;
}
