#pragma once

#include "Redflag/AI/NpcSystem.h"
#include "Redflag/Scene/Entity.h"
#include <thread>

inline void CheckWorldActions(void (*check)(bool, const char*))
{
    using namespace Redflag;
    Scene room;
    Entity teammate = room.CreateEntityWithUUID(1, "Teammate");
    teammate.AddComponent<HealthComponent>().Team = "crew";
    teammate.AddComponent<InventoryComponent>();
    teammate.AddComponent<RigidBody3DComponent>();
    teammate.AddComponent<BoxCollider3DComponent>();

    Entity player = room.CreateEntityWithUUID(2, "Player");
    player.AddComponent<HealthComponent>().Team = "crew";
    player.AddComponent<InventoryComponent>();
    player.GetComponent<TransformComponent>().Translation.x = 1.5f;
    player.AddComponent<InteractionComponent>() = {"revive", 2, 1, 40, true};

    Entity supply = room.CreateEntityWithUUID(3, "Supplies");
    supply.AddComponent<PickupComponent>() = {"medkit", 2};
    supply.GetComponent<TransformComponent>().Translation.x = 1.0f;
    room.OnSimulationStart();
    check(CollectItem(room, 1, 3, 2) == ActionResult::Completed && !room.GetEntityByUUID(3), "physical pickup becomes inventory");
    check(GiveItem(room, 1, 2, "medkit", 1, 2) == ActionResult::Completed, "teammates exchange one item");
    check(teammate.GetComponent<InventoryComponent>().Items.at("medkit") == 1 && player.GetComponent<InventoryComponent>().Items.at("medkit") == 1, "total stock conserved");

    check(!ApplyDamage(room, 1, 2, 100), "team prevents damage");
    check(ApplyDamage(room, 0, 2, 125), "trap damage downs player");
    Interaction rescue;
    check(rescue.Start(room, 1, 2, "revive") == ActionResult::Running, "start timed rescue");
    rescue.Update(room, .4f);
    rescue.Cancel();
    check(rescue.Update(room, 2) == ActionResult::Cancelled && player.GetComponent<HealthComponent>().Current == 0, "cancelled rescue has no effect");
    rescue.Start(room, 1, 2, "revive");
    check(rescue.Update(room, 1) == ActionResult::Completed && player.GetComponent<HealthComponent>().Current == 40, "revive restores configured health once");
    rescue.Update(room, 2);
    check(player.GetComponent<HealthComponent>().Current == 40, "completed effect cannot replay");

    Entity device = room.CreateEntityWithUUID(4, "Console");
    device.AddComponent<HealthComponent>().Current = 15;
    device.AddComponent<InteractionComponent>() = {"repair", 2, 2, 30, false};
    device.GetComponent<TransformComponent>().Translation.x = 1.5f;
    rescue.Start(room, 1, 4, "repair");
    check(rescue.Update(room, 2) == ActionResult::Completed && device.GetComponent<HealthComponent>().Current == 45, "repair uses same interaction");
    rescue.Start(room, 1, 4, "repair");
    room.DestroyEntity(device);
    check(rescue.Update(room, 2) == ActionResult::MissingTarget, "destroyed target cannot complete");
    room.OnSimulationStop();
}

// 从正常决策客户端发出同一句指令，读取角色走过的动作和两个人的库存。
inline bool CheckCompanionGame(uint16_t port, bool hasMedicine)
{
    using namespace Redflag;
    Scene room;
    Entity actor = room.CreateEntityWithUUID(41, "瓦特");
    actor.AddComponent<NpcComponent>() = {"assets/ai/watt.npc.yaml", "", 42};
    actor.AddComponent<HealthComponent>().Team = "crew";
    auto& pack = actor.AddComponent<InventoryComponent>().Items;
    Entity player = room.CreateEntityWithUUID(42, "队友");
    player.AddComponent<InventoryComponent>();
    player.AddComponent<HealthComponent>().Team = "crew";
    player.GetComponent<TransformComponent>().Translation.x = 7;
    if (hasMedicine) pack["medkit"] = 1;
    else
    {
        Entity box = room.CreateEntityWithUUID(43, "医疗包");
        box.AddComponent<PickupComponent>() = {"medkit", 1};
        box.GetComponent<TransformComponent>().Translation.x = 2.5f;
    }

    room.OnSimulationStart();
    NpcSystem npc(room, port);
    npc.Submit(41, "帮我留一份医疗包，等我过来再给我。");
    bool waited = false;
    bool contactMatches = true, sawPickup = false, sawGive = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(70);
    while (std::chrono::steady_clock::now() < deadline)
    {
        npc.Update(1.0f/60.0f);
        room.OnUpdatePhysics(1.0f/60.0f);
        const NpcState& state = *npc.Inspect(41);
        if (state.Stage == "pickup" || state.Stage == "give")
        {
            const AnimationState& clip = *npc.GetDefinition(41)->Controller.Find(state.Stage);
            const bool due = state.PhaseTime >= clip.Seconds * clip.Contact;
            const auto& received = player.GetComponent<InventoryComponent>().Items;
            const bool exchanged = state.Stage == "pickup" ? !room.GetEntityByUUID(43) : received.count("medkit") != 0;
            contactMatches &= exchanged == due;
            bool& observed = state.Stage == "pickup" ? sawPickup : sawGive;
            if (exchanged && !observed)
            {
                observed = true;
                std::cout << "CONTACT\t" << state.Stage << '\t' << state.PhaseTime << '\t' << clip.Seconds << '\n';
            }
        }
        if (state.Action == "reserve" && !waited)
        {
            waited = true;
            player.GetComponent<TransformComponent>().Translation = actor.GetComponent<TransformComponent>().Translation + glm::vec3(1,0,0);
        }
        if (!state.Request && state.Action.empty()) break;
        if (state.Action == "follow") break;
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }
    bool gathered = false;
    for (const NpcEvent& event : npc.Inspect(41)->Events)
    {
        std::cout << "EVENT\t" << event.Time << '\t' << event.Kind << '\t' << event.Text << '\t' << event.Target << '\n';
        if (event.Kind == "完成" && event.Text == "gather") gathered = true;
    }
    const bool delivered = player.GetComponent<InventoryComponent>().Items["medkit"] == 1 && pack.empty();
    room.OnSimulationStop();
    return waited && delivered && gathered == !hasMedicine && contactMatches && sawGive && sawPickup == !hasMedicine;
}
