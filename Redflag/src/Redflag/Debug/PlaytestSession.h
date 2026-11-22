#pragma once

#include "PlaytestInput.h"
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <vector>

namespace Redflag
{
    class Scene;
    class Event;

    struct PlaytestInfo
    {
        std::filesystem::path Directory;
        std::string Scene, Intent, Owner;
        uint64_t Generation = 0, Seq = 0;
        double Seconds = 0, FrameMs = 0;
    };

    /* 一次播放拥有一个会话。关卡和脚本照常运行，记录器只在设置 REDFLAG_PLAYTEST 时打开文件。 */
    class PlaytestSession
    {
      public:
        static std::unique_ptr<PlaytestSession> Open(const std::filesystem::path &scene);
        static PlaytestSession *Active();
        ~PlaytestSession();

        // ---- 一帧中的输入与现场记录 ----
        void BeginFrame(double seconds);
        void RecordScene(Scene &scene);
        void HumanInput(const Redflag::Event &event);
        void SendToGui();
        void Event(uint64_t entity, const std::string &kind, const std::string &text);
        const PlaytestInfo &Info() const { return m_Info; }
        const PlaytestInput &Input() const { return m_Input; }

        // ---- 人工接管与回看批注 ----
        bool HandOff(const std::string &owner);
        void Note(uint64_t seq, const std::string &entity, const std::string &text);
        void Mark(const std::string &text) { Note(m_Info.Seq, "", text); }

      private:
        PlaytestSession() = default;
        void Command();
        void SaveStatus(const char *state);
        void SaveOwner();
        struct Entry
        {
            std::string Entity, Kind, Text;
        };
        struct InputSample
        {
            std::string Owner, Op, Key;
            unsigned Command = 0;
            std::optional<std::pair<float, float>> Position;
        };

        PlaytestInfo m_Info;
        PlaytestInput m_Input;
        std::ofstream m_Frames;
        std::vector<Entry> m_Events;
        std::vector<InputSample> m_Inputs;
        std::optional<std::pair<float, float>> m_HumanPointer;
        std::map<uint64_t, uint64_t> m_NpcEvents;
        unsigned m_LastCommand = 0;
        std::filesystem::file_time_type m_CommandDirectoryTime{};
        void *m_Writer = nullptr;
    };
} // namespace Redflag
