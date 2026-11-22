#pragma once

#include "Redflag/AI/NpcSystem.h"
#include "Redflag/Scripting/NpcScriptAction.h"
#include "Redflag/Scene/Entity.h"
#include <thread>

inline void CheckNpcRuntime(uint16_t port, const std::filesystem::path& work)
{
    using namespace Redflag;
    std::filesystem::current_path(work);
    ScriptEngine::Init();
    Scene room;
    Entity technician = room.CreateEntityWithUUID(81, "Technician");
    technician.AddComponent<NpcComponent>().Definition = "technician.npc.yaml";
    technician.AddComponent<ScriptComponent>().ClassName = "Workshop.RepairOrder";
    Entity lift = room.CreateEntityWithUUID(82, "Lift");
    lift.GetComponent<TransformComponent>().Translation.z = 1.f;
    lift.AddComponent<HealthComponent>().Current = 40.f;
    lift.AddComponent<InteractionComponent>() = {"repair", 2.f, 1.f, 30.f, false};
    room.OnRuntimeStart(port);
    NpcSystem& actors = *room.GetNpcs();

    auto advanceUntil = [&](const std::function<bool()>& done) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        do
        {
            room.UpdateGame(1.f / 60.f);
            if (done()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        } while (std::chrono::steady_clock::now() < deadline);
        return false;
    };
    auto repairing = [&]() {
        const NpcState* state = actors.Inspect(81);
        return state && state->Use.IsRunning();
    };

    // C# OnCreate 已发出第一条任务。在动作进行中取消，设备生命应停在原值。
    Check(advanceUntil(repairing), "managed script submitted and started repair");
    Check(actors.Cancel(81) && !actors.Cancel(81), "cancel running action exactly once");
    for (int frame = 0; frame < 75; ++frame) room.UpdateGame(1.f / 60.f);
    Check(lift.GetComponent<HealthComponent>().Current == 40.f, "cancelled repair leaves lift unchanged");

    Check(actors.Submit(81, "继续修理电梯。"), "host starts second repair");
    Check(advanceUntil([&]() { return lift.GetComponent<HealthComponent>().Current == 70.f; }),
        "completed repair changes lift health once");
    room.UpdateGame(1.f / 60.f); // 让脚本收到刚才完成的维修回执。
    uint64_t cursor = 0;
    int completed = 0;
    while (const NpcEvent* entry = actors.NextEvent(81, cursor))
    {
        if (entry->Kind == "完成" && entry->Text == "repair" && entry->Target == 82 && entry->Speech == "设备修好了。")
            ++completed;
    }
    uint64_t secondReader = 0;
    Check(completed == 1 && !actors.NextEvent(81, cursor) && actors.NextEvent(81, secondReader),
        "independent readers see one completion, target and configured reply");

    // 同一个模型选择在行为树里改接到 C# 动作。读数不改变电梯的耐久。
    Check(NpcScriptAction::Exists("Workshop.ReadGauge"), "managed action is available in the editor catalog");
    actors.GetDefinition(81)->Tree.SetAction(2, "Workshop.ReadGauge");
    actors.Submit(81, "检查电梯。");
    Check(advanceUntil([&]() { return bool(actors.Inspect(81)->Script); }), "tree started managed action");
    Check(actors.Cancel(81), "managed action interrupted before taking a reading");

    actors.Submit(81, "重新检查电梯。");
    bool read = false;
    Check(advanceUntil([&]() {
        while (const NpcEvent* entry = actors.NextEvent(81, cursor))
            if (entry->Kind == "完成" && entry->Text == "Workshop.ReadGauge" && entry->Target == 82) read = true;
        return read;
    }) && lift.GetComponent<HealthComponent>().Current == 70.f, "new managed instance completed without applying repair");

    // 再试一次尚在等待模型的任务。移除脚本会调用示例里的 C# OnDestroy 和 Npc.Cancel。
    Check(actors.Submit(81, "再次检查电梯。"), "another request is pending");
    technician.RemoveComponent<ScriptComponent>();
    Check(!actors.Inspect(81)->Request && !actors.Cancel(81), "managed cancel removes pending request");
    Check(!actors.Submit(81, "") && !actors.Submit(999, "修理"), "empty or unknown requests are rejected");
    room.OnRuntimeStop();
    Check(!room.GetNpcs() && !ScriptEngine::IsRuntimeActive(), "scene stop releases its NPC runtime");
    ScriptEngine::Shutdown();
}
