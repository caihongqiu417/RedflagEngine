#include "rfpch.h"
#include "Redflag/AI/NpcSystem.h"
#include "Redflag/AI/SpeechInput.h"
#include "Redflag/AI/SpeechPlayback.h"
#include "Redflag/Core/Log.h"
#include "Redflag/Scene/Entity.h"
#include <fstream>
#include <thread>

using namespace Redflag;
using Clock = std::chrono::steady_clock;

int main(int argc, char** argv)
{
    if (argc != 5) return 2;
    Log::Init();
    const uint16_t modelPort = uint16_t(std::stoi(argv[2]));
    const int count = std::stoi(argv[3]);
    std::ifstream file(std::filesystem::u8path(argv[4]), std::ios::binary);
    std::string wav((std::istreambuf_iterator<char>(file)), {});
    if (wav.empty()) return 2;

    Scene room;
    Entity actor = room.CreateEntityWithUUID(41, "瓦特");
    actor.AddComponent<NpcComponent>() = {"assets/ai/watt.npc.yaml", "", 42};
    actor.AddComponent<HealthComponent>().Team = "crew";
    actor.AddComponent<InventoryComponent>();
    Entity player = room.CreateEntityWithUUID(42, "队友");
    player.AddComponent<HealthComponent>().Team = "crew";
    player.AddComponent<InventoryComponent>();
    // 将队友放在瓦特正前方一米。留药命令通过后，可以直接看到递药动作，库存随后改变。
    player.GetComponent<TransformComponent>().Translation.z = 1.f;
    room.OnSimulationStart();
    SpeechInput input;
    SpeechPlayback voice;
    bool passed = true;
    for (int trial = 1; trial <= count; ++trial)
    {
        NpcSystem npcs(room, modelPort);
        actor.GetComponent<InventoryComponent>().Items["medkit"] = 1;
        player.GetComponent<InventoryComponent>().Items.clear();
        uint64_t cursor = 0;
        double text = -1, choice = -1, action = -1, stock = -1, receipt = -1, audio = -1, stop = -1;
        bool submitted = false;
        std::string selected;
        const Clock::time_point begin = Clock::now();
        const auto ms = [&]() { return std::chrono::duration<double, std::milli>(Clock::now() - begin).count(); };
        input.SubmitWav(wav);
        for (int frame = 1; ms() < 20000; ++frame)
        {
            if (const std::optional<SpeechEvent> heard = input.Take())
            {
                if (heard->Type != SpeechEventType::Transcript) break;
                text = ms();
                submitted = npcs.Submit(41, heard->Text);
                if (!submitted) break;
            }
            if (submitted)
            {
                npcs.Update(1.f / 60.f);
                while (const NpcEvent* event = npcs.NextEvent(41, cursor))
                {
                    if (event->Kind == "选择" && choice < 0) { choice = ms(); selected = event->Text; }
                    if (event->Kind == "动作" && event->Text == "give" && action < 0) action = ms();
                    if (event->Kind == "库存" && stock < 0) stock = ms();
                    if (event->Kind == "完成" && !event->Speech.empty()) { receipt = ms(); voice.Speak(event->Speech); }
                }
                voice.Update();
                if (receipt >= 0 && voice.FirstAudioMs() >= 0)
                {
                    audio = ms();
                    voice.Stop();
                    stop = ms() - audio;
                    break;
                }
                if (!voice.Error().empty()) break;
            }
            // 用真实时钟每秒推进 60 帧，递药动画按原动作表的接触和结束时刻播放。
            std::this_thread::sleep_until(begin + std::chrono::microseconds(int64_t(frame) * 1000000 / 60));
        }
        std::cout << "VOICE\t" << trial << '\t' << selected << '\t' << text << '\t' << choice << '\t' << action
            << '\t' << stock << '\t' << receipt << '\t' << audio << '\t' << stop << std::endl;
        passed &= selected == "reserve → reserve" && audio >= 0 && player.GetComponent<InventoryComponent>().Items["medkit"] == 1;
    }
    room.OnSimulationStop();
    return passed ? 0 : 1;
}
