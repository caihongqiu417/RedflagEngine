#pragma once

#include "ReviewImage.h"
#include "Redflag/AI/SpeechPlayback.h"
#include <filesystem>
#include <future>
#include <map>
#include <optional>
#include <string>
#include <yaml-cpp/yaml.h>

namespace Redflag
{
    class VulkanContext;

    /* 帧列表只记时间和原文件的位置。拖动进度条时才读取该帧的角色、操作与截图。 */
    class PlaytestReview
    {
      public:
        void Select(const std::string& session);
        void StopAudio();
        void Draw(VulkanContext& context, VulkanDraw& device);

      private:
        struct Frame
        {
            uint64_t Seq;
            double Seconds;
            std::streamoff Offset;
        };

        struct Media
        {
            std::string File, Audio;
            uint64_t LastSeq = 0;
        };

        struct Track
        {
            std::vector<Frame> Frames;
            std::map<uint64_t, Media> Files;
            std::map<uint64_t, std::vector<YAML::Node>> VoiceEvents;
            std::streamoff FrameOffset = 0, MediaOffset = 0;
        };

        struct Picture
        {
            std::filesystem::path File;
            ReviewImage Image;
            std::string Error;
        };

        void Open(const std::filesystem::path& folder);
        void Poll();
        YAML::Node ReadFrame(int frame) const;
        void DrawControl();
        void DrawTimeline();
        void OpenBaseline(const std::filesystem::path& folder);
        static void ReadTrack(Track& track, const std::filesystem::path& folder);
        void DrawFrame(VulkanContext& context, VulkanDraw& device);
        void DrawPicture(const Track& track, const std::filesystem::path& folder, int frame, Picture& cached,
                         VulkanContext& context, VulkanDraw& device);
        void DrawFeedback();
        void DrawVoice(uint64_t seq);
        void Submit(const char* action);

        std::filesystem::path m_Project, m_Folder;
        std::vector<std::filesystem::path> m_Sessions;
        std::string m_Requested, m_Intent, m_Scene, m_Owner, m_State, m_Message, m_Entity;
        Track m_Track, m_Reference;
        std::filesystem::path m_ReferenceFolder;
        std::string m_RequestedBase;
        std::optional<uint64_t> m_RequestedFrame, m_RequestedBaseFrame;
        std::vector<YAML::Node> m_Notes;
        YAML::Node m_Current;
        std::future<YAML::Node> m_Task;
        std::filesystem::path m_TaskFolder;
        std::streamoff m_NoteOffset = 0;
        int m_Selected = -1, m_Loaded = -1, m_Baseline = -1;
        uint64_t m_Generation = 0, m_PendingCommand = 0;
        double m_NextPoll = 0, m_PlayerClock = 0;
        bool m_Follow = true, m_Playing = false;
        char m_Comment[2048]{};
        Picture m_Now, m_Before;
        Scope<SpeechPlayback> m_Speech;
        bool m_RawAudio = false;
    };
} // namespace Redflag
