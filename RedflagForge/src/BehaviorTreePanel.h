#pragma once

#include "Redflag/AI/BehaviorTree.h"

namespace Redflag
{
    class BehaviorTreePanel
    {
      public:
        void Draw(BehaviorTree& tree, const std::filesystem::path& asset, const BehaviorTrace* trace = nullptr, const char* title = "行为树",
                  const BehaviorBlackboard* board = nullptr, const BehaviorRun* run = nullptr);

      private:
        void Select(const BehaviorNode& node);
        void DrawCanvas(BehaviorTree& tree, const BehaviorTrace* trace, uint32_t aborted);
        void DrawNodeSettings(BehaviorTree& tree);

        uint32_t m_Selected = 0, m_From = 0;
        glm::vec2 m_Pan{0, 0};
        char m_Question[512]{}, m_Name[256]{}, m_Action[128]{}, m_Key[128] = "gather";
        char m_Fact[256]{}, m_Value[256]{};
        BehaviorTrace m_Preview;
        std::string m_FromKey;
        bool m_ShowPreview = false;
        std::string m_Message;
    };
} // namespace Redflag
