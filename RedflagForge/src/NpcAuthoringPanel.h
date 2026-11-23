#pragma once

#include "Redflag/AI/DecisionClient.h"
#include "Redflag/AI/NpcDefinition.h"
#include "BehaviorTreePanel.h"
#include <functional>
#include <future>
#include <yaml-cpp/yaml.h>

namespace Redflag
{
    class ModelAnimation;
    struct NpcTeammateCheck;

    /* 编辑角色定义。保存角色属性时，行为树连线仍由行为树面板单独保存。 */
    class NpcAuthoringPanel
    {
    public:
        ~NpcAuthoringPanel();
        std::function<void(const std::filesystem::path&)> OpenMontage;

        static void DrawTeammateCheck(const NpcIntent& intent, const NpcTeammateCheck& check);

        bool Draw(std::string& definitionPath, const BehaviorTrace* trace = nullptr, const std::string& stage = {},
                  const std::vector<std::string>& modelClips = {}, const ModelAnimation* model = nullptr,
                  const std::function<void(const NpcDefinition&)>& drawSceneCheck = {}, bool* open = nullptr);

    private:
        void Open(const std::filesystem::path& path);
        void Start(YAML::Node request);
        void DrawProfile();
        void DrawIntents();
        void DrawAnimations(const std::string& stage, const std::vector<std::string>& modelClips);
        void DrawAnimationEvents(const ModelAnimation* model);
        void SaveAnimations();
        void DrawTables();
        void DrawPreview();
        void StopAudio();

        std::filesystem::path m_Path, m_Audio, m_TaskOwner, m_RequestOwner;
        NpcDefinition m_Saved, m_Draft;
        std::vector<AnimationState> m_States;
        std::string m_EventState;
        int m_PreviewEvent = -1;
        bool m_PreviewEnd = false;
        std::future<YAML::Node> m_Task;
        DecisionClient m_Client;
        BehaviorTreePanel m_Tree;
        bool m_ShowTree = false;
        uint64_t m_Request = 0;
        std::vector<std::string> m_Voices;
        std::string m_Message, m_Operation, m_CopyName, m_CopyFile, m_DefaultVoice;
        std::string m_Prompt = "请帮我保留一份医疗包。";
        std::string m_Sample = "我在这里等你。";
        bool m_Loaded = false, m_PlayOnReady = false;
        int m_Table = 0;
    };
}
