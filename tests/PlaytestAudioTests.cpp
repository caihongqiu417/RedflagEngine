#include "rfpch.h"
#include "Redflag/Core/Log.h"
#include "Redflag/Debug/PlaytestAudio.h"
#include "Redflag/Debug/PlaytestSession.h"
#include <thread>
#include <yaml-cpp/yaml.h>

using namespace Redflag;

int main(int argc, char** argv)
{
    if (argc != 3) return 2;
    Log::Init();
    _wputenv_s(L"REDFLAG_PLAYTEST",L"");
    if (PlaytestSession::Open("sound-test") || ObserveGameSpeech(41)) return 1;

    const std::filesystem::path folder = std::filesystem::u8path(argv[2]);
    _wputenv_s(L"REDFLAG_PLAYTEST",folder.c_str());
    std::unique_ptr<PlaytestSession> session = PlaytestSession::Open("sound-test");
    SpeechPlayback voice;
    auto tick = [&](){
        session->BeginFrame(1.0/60);
        voice.Update();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    };
    voice.Speak("这句话在合成结束前取消。", "", ObserveGameSpeech(41));
    voice.Stop();
    voice.Speak("回收装置正在启动，请稍等片刻。", "", ObserveGameSpeech(42));
    const auto timeout = std::chrono::steady_clock::now()+std::chrono::seconds(20);
    while (voice.FirstAudioMs()<0 && std::chrono::steady_clock::now()<timeout) tick();
    if (voice.FirstAudioMs()<0) return 1;
    session->HandOff("human");
    voice.Stop();
    voice.Speak("设备就绪。", "", ObserveGameSpeech(43));
    while ((voice.FirstAudioMs()<0 || voice.Playing()) && std::chrono::steady_clock::now()<timeout) tick();
    session.reset();

    std::ifstream input(folder/"media.jsonl");
    std::map<std::string,std::vector<std::string>> stages;
    for (std::string line; std::getline(input,line); )
    {
        YAML::Node r=YAML::Load(line);
        const std::string who=r["entity"].as<std::string>(), event=r["event"].as<std::string>();
        stages[who].push_back(event);
        if (who=="41" && !r["file"].IsNull()) return 1;
        if (event=="stopped" || event=="finished")
        {
            const int64_t total=(std::filesystem::file_size(folder/r["file"].as<std::string>())-44)/2;
            const int64_t samples=r["device_samples"].as<int64_t>();
            if (samples<=0 || samples>total || (event=="stopped" && samples==total)) return 1;
            if (event=="finished" && samples!=total) return 1;
            std::cout << who << ' ' << event << " samples " << samples << '/' << total << '\n';
        }
    }
    return stages["41"]==std::vector<std::string>{"requested","cancelled"} &&
        stages["42"]==std::vector<std::string>{"requested","submitted","first_voice","stopped"} &&
        stages["43"]==std::vector<std::string>{"requested","submitted","first_voice","finished"} ? 0 : 1;
}
