#include "rfpch.h"
#include "AnimationTransitionPanel.h"
#include "../NpcAuthoringFiles.h"
#include "Redflag/Utils/PlatformUtils.h"
#include <imgui.h>

namespace Redflag
{
    static bool ChooseState(const char* label, std::string& id, const AnimationController& controller)
    {
        bool changed = false;
        if (ImGui::BeginCombo(label, id.c_str()))
        {
            for (const AnimationState& state : controller.GetStates())
                if (ImGui::Selectable(state.ID.c_str(), state.ID == id)) { id = state.ID; changed = true; }
            ImGui::EndCombo();
        }
        return changed;
    }

    void AnimationTransitionPanel::DrawMenu()
    {
        if (ImGui::MenuItem("动画过渡…")) m_Open = true;
    }

    void AnimationTransitionPanel::Select()
    {
        if (const AnimationTransition* saved = m_Controller.FindTransition(m_Draft.From, m_Draft.To))
            m_Draft = *saved;
        else if (const AnimationState* target = m_Controller.Find(m_Draft.To))
        {
            m_Draft.Seconds = target->Blend;
            m_Draft.Control = AnimationTransition{}.Control;
        }
    }

    void AnimationTransitionPanel::Load(const std::filesystem::path& path)
    {
        if (!m_Controller.Load(path, m_Message)) return;
        const auto& states = m_Controller.GetStates();
        if (states.empty())
        {
            m_Path.clear();
            m_Message = "这份动作表还没有状态。";
            return;
        }
        m_Path = path;
        if (!m_Controller.Find(m_Draft.From)) m_Draft.From = states.front().ID;
        if (!m_Controller.Find(m_Draft.To)) m_Draft.To = states.size() > 1 ? states[1].ID : states.front().ID;
        Select();
    }

    static YAML::Node Settings(const AnimationTransition* transition)
    {
        YAML::Node values;
        if (transition)
        {
            values["Seconds"] = transition->Seconds;
            values["Control"].push_back(transition->Control.x);
            values["Control"].push_back(transition->Control.y);
        }
        return values;
    }

    void AnimationTransitionPanel::Save(bool remove)
    {
        YAML::Node request;
        request["operation"] = "transition";
        request["profile"] = std::filesystem::absolute(m_Path).u8string();
        request["from"] = m_Draft.From;
        request["to"] = m_Draft.To;
        request["original"] = Settings(m_Controller.FindTransition(m_Draft.From, m_Draft.To));
        request["value"] = Settings(remove ? nullptr : &m_Draft);
        m_Save = std::async(std::launch::async, RunNpcAuthoringTool, request);
        m_Message = "正在保存…";
    }

    void AnimationTransitionPanel::Draw()
    {
        if (!m_Open) return;
        if (m_Save.valid() && m_Save.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            const YAML::Node result = m_Save.get();
            if (result["error"]) m_Message = result["error"].as<std::string>();
            else { Load(m_Path); m_Message = "已保存。重新开始试玩后使用新曲线。"; }
        }
        ImGui::SetNextWindowSize({540, 440}, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("动画过渡", &m_Open))
        {
            ImGui::BeginDisabled(m_Save.valid());
            if (ImGui::Button("打开动作表…"))
            {
                const std::string file = FileDialogs::OpenFile("动画状态 (*.animator.yaml)\0*.animator.yaml\0");
                if (!file.empty()) Load(std::filesystem::u8path(file));
            }
            if (!m_Path.empty())
            {
                ImGui::SameLine();
                ImGui::TextUnformatted(m_Path.filename().u8string().c_str());
                if (ChooseState("离开状态", m_Draft.From, m_Controller)) Select();
                if (ChooseState("进入状态", m_Draft.To, m_Controller)) Select();
                const bool saved = m_Controller.FindTransition(m_Draft.From, m_Draft.To) != nullptr;
                const bool montage = m_Controller.Find(m_Draft.From)->MontageAsset || m_Controller.Find(m_Draft.To)->MontageAsset;
                if (montage) ImGui::TextWrapped("这个动作使用蒙太奇。打开「窗口 → 动画蒙太奇」，在「淡入淡出」里调整它的播放曲线。");
                ImGui::BeginDisabled(montage);
                ImGui::TextDisabled("%s", saved ? "这对状态有独立的过渡" : "当前沿用目标状态的时长和引擎默认曲线");
                ImGui::InputFloat("淡出时间（秒）", &m_Draft.Seconds, .01f, .1f, "%.3f");
                ImGui::SliderFloat("起始控制点", &m_Draft.Control.x, 0, 1);
                ImGui::SliderFloat("结束控制点", &m_Draft.Control.y, 0, 1);
                ImGui::TextUnformatted("从左到右是时间，从下到上是新动作占的比重");
                float weights[65];
                for (int i = 0; i < IM_ARRAYSIZE(weights); ++i)
                    weights[i] = m_Draft.Weight(float(i) / (IM_ARRAYSIZE(weights) - 1));
                ImGui::PlotLines("##混合曲线", weights, IM_ARRAYSIZE(weights), 0, nullptr, 0, 1, {-1, 90});
                ImGui::BeginDisabled(m_Draft.From == m_Draft.To || !(m_Draft.Seconds >= 0.f));
                if (ImGui::Button("保存这条过渡")) Save(false);
                ImGui::EndDisabled();
                ImGui::EndDisabled();
                if (saved)
                {
                    ImGui::SameLine();
                    if (ImGui::Button("恢复引擎默认")) Save(true);
                }
            }
            ImGui::EndDisabled();
            if (!m_Message.empty()) ImGui::TextWrapped("%s", m_Message.c_str());
        }
        ImGui::End();
    }
}
