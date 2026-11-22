#pragma once

#include "Redflag/AI/DecisionClient.h"
#include "Redflag/AI/NpcDefinition.h"
#include <future>
#include <yaml-cpp/yaml.h>

namespace Redflag
{
    /* 编辑角色定义。保存角色属性时，行为树连线仍由行为树面板单独保存。 */
    class NpcAuthoringPanel
    {
    public:
        ~NpcAuthoringPanel();

        bool Draw(std::string& definitionPath, const BehaviorTrace* trace = nullptr, const std::string& stage = {},
                  const std::vector<std::string>& modelClips = {});

    private:
        void Open(const std::filesystem::path& path);
        void Start(YAML::Node request);
        void DrawProfile();
        void DrawIntents();
        void DrawAnimations(const std::string& stage, const std::vector<std::string>& modelClips);
        void DrawAnimationEvents();
        void SaveAnimations();
        void DrawTables();
        void DrawPreview();
        void StopAudio();

        std::filesystem::path m_Path, m_Audio, m_TaskOwner, m_RequestOwner;
        NpcDefinition m_Saved, m_Draft;
        std::vector<AnimationState> m_States;
        std::string m_EventState;
        std::future<YAML::Node> m_Task;
        DecisionClient m_Client;
        uint64_t m_Request = 0;
        std::vector<std::string> m_Voices;
        std::string m_Message, m_Operation, m_CopyName, m_CopyFile, m_DefaultVoice;
        std::string m_Prompt = "请帮我保留一份医疗包。";
        std::string m_Sample = "我在这里等你。";
        bool m_Loaded = false, m_PlayOnReady = false;
        int m_Table = 0;
    };
}
