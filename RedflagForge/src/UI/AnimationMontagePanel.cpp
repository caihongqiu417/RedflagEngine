#include "rfpch.h"
#include "AnimationMontagePanel.h"
#include "AnimationFields.h"
#include "../AnimationPoseView.h"
#include "Redflag/Animation/ModelAnimation.h"
#include "Redflag/Scene/Entity.h"
#include "Redflag/Scene/Components.h"
#include "Redflag/Utils/PlatformUtils.h"

namespace Redflag
{
    namespace fs = std::filesystem;
    using AnimationFields::Edit;

    //========== 打开、创建与保存 ==========

    void AnimationMontagePanel::DrawMenu()
    {
        if (ImGui::MenuItem("动画蒙太奇…")) m_Open = true;
    }

    void AnimationMontagePanel::Open(const fs::path& file)
    {
        m_Open = true;
        if (m_Dirty)
        {
            m_Message = "请先保存或撤销手头的修改，再打开另一份蒙太奇。";
            return;
        }
        if (!m_Draft.Load(file, m_Message)) return;
        std::error_code error;
        m_OpenedAt = fs::last_write_time(file, error);
        if (error) { m_Message = "无法读取文件的修改时间。"; return; }
        m_Path = file;
        m_SavedClip = m_Draft.Clip;
        m_SavedEvents = m_Draft.Events;
        m_Preview = -1;
        m_Editing = true;
    }

    void AnimationMontagePanel::New(const ModelAnimation* model)
    {
        m_Path.clear();
        m_NewFile = "assets/untitled.montage.yaml";
        m_Draft = {};
        m_SavedClip.clear();
        m_SavedEvents.clear();
        m_Preview = -1;
        m_Draft.Name = "新动作";
        m_Draft.Slot = "FullBody";
        m_Draft.Sections.push_back({"Main"});
        if (model && !model->GetClips().empty())
        {
            const ModelAnimation::Clip& clip = model->GetClips().front();
            m_Draft.Clip = clip.Name;
            m_Draft.Sections.front().End = clip.Duration;
        }
        m_Message.clear();
        m_Editing = true;
        m_Dirty = true;
    }

    void AnimationMontagePanel::Save()
    {
        const fs::path file = m_Path.empty() ? fs::u8path(m_NewFile) : m_Path;
        if (file.stem().extension() != ".montage" || file.extension() != ".yaml")
        {
            m_Message = "文件名应以 .montage.yaml 结尾。";
            return;
        }
        // 人和工具都能编辑资产。防止当前窗口覆盖打开后被人改过或移走的文件。
        std::error_code error;
        if (m_Path.empty() && fs::exists(file, error))
        {
            m_Message = "这个名字已被使用。换个文件名，或打开已有蒙太奇。";
            return;
        }
        if (!m_Path.empty() && fs::last_write_time(file, error) != m_OpenedAt)
        {
            m_Message = "磁盘上的文件已更改。撤销本次编辑后可重新载入。";
            return;
        }
        if (!m_Draft.Save(file, m_Message)) return;
        m_Dirty = false;
        Open(file);
        if (m_Message.empty()) m_Message = "已保存并重新载入。";
    }

    //========== 原片与动作范围 ==========

    void AnimationMontagePanel::DrawClip(const ModelAnimation* model)
    {
        m_Dirty |= Edit("动作名称", m_Draft.Name);
        if (model && !model->GetClips().empty())
        {
            if (ImGui::BeginCombo("原片", m_Draft.Clip.c_str()))
            {
                for (const ModelAnimation::Clip& clip : model->GetClips())
                    if (ImGui::Selectable(clip.Name.c_str(), clip.Name == m_Draft.Clip))
                    {
                        m_Draft.Clip = clip.Name;
                        m_Dirty = true;
                    }
                ImGui::EndCombo();
            }
            const auto& clips = model->GetClips();
            const auto clip = std::find_if(clips.begin(), clips.end(), [&](const ModelAnimation::Clip& c) { return c.Name == m_Draft.Clip; });
            if (clip != clips.end()) ImGui::TextDisabled("所选模型中的原片长 %.3f 秒", clip->Duration);
            else ImGui::TextWrapped("所选模型没有这段原片。可从列表另选，或在层级面板选择使用它的模型。");
        }
        else
        {
            m_Dirty |= Edit("原片", m_Draft.Clip);
            ImGui::TextDisabled("在层级面板选择带动画的模型，可以直接挑选原片。");
        }

        m_Dirty |= Edit("动作插槽（Slot）", m_Draft.Slot);
        ImGui::TextWrapped("同一角色上，同一个插槽里的新动作会接替旧动作。");
        ImGui::Separator();
        ImGui::TextUnformatted("骨骼遮罩");
        ImGui::TextWrapped("每项填一根骨骼，它和下面的关节一起播放这段原片。留空时使用全身。");
        for (int i = 0; i < (int)m_Draft.Bones.size();)
        {
            ImGui::PushID(i);
            m_Dirty |= Edit("##根骨", m_Draft.Bones[i]);
            ImGui::SameLine();
            bool remove = ImGui::SmallButton("移除");
            ImGui::PopID();
            if (remove) { m_Draft.Bones.erase(m_Draft.Bones.begin() + i); m_Dirty = true; }
            else ++i;
        }
        if (ImGui::Button("添加骨骼")) { m_Draft.Bones.emplace_back(); m_Dirty = true; }
    }

    void AnimationMontagePanel::DrawSections()
    {
        ImGui::TextWrapped("时间从原片开头算起。默认从第一段开始，播到末尾时进入选定的下一段。");
        for (int i = 0; i < (int)m_Draft.Sections.size();)
        {
            MontageSection& section = m_Draft.Sections[i];
            ImGui::PushID(i);
            ImGui::Separator();
            ImGui::TextDisabled(i == 0 ? "第 %d 段（默认起点）" : "第 %d 段", i + 1);
            const std::string oldName = section.Name;
            if (Edit("段名", section.Name))
            {
                for (MontageSection& other : m_Draft.Sections)
                    if (!oldName.empty() && other.Next == oldName) other.Next = section.Name;
                m_Dirty = true;
            }
            m_Dirty |= ImGui::InputFloat("开始（秒）", &section.Start, 0, 0, "%.3f");
            m_Dirty |= ImGui::InputFloat("结束（秒）", &section.End, 0, 0, "%.3f");
            if (ImGui::BeginCombo("下一段", section.Next.empty() ? "结束播放" : section.Next.c_str()))
            {
                if (ImGui::Selectable("结束播放", section.Next.empty())) { section.Next.clear(); m_Dirty = true; }
                for (const MontageSection& other : m_Draft.Sections)
                    if (!other.Name.empty() && ImGui::Selectable(other.Name.c_str(), section.Next == other.Name))
                    { section.Next = other.Name; m_Dirty = true; }
                ImGui::EndCombo();
            }
            bool remove = ImGui::SmallButton("移除播放段");
            ImGui::PopID();
            if (remove)
            {
                const std::string removed = section.Name;
                m_Draft.Sections.erase(m_Draft.Sections.begin() + i);
                for (MontageSection& other : m_Draft.Sections)
                    if (other.Next == removed) other.Next.clear();
                m_Dirty = true;
            }
            else ++i;
        }
        if (ImGui::Button("添加播放段"))
        {
            float end = 0;
            for (const MontageSection& section : m_Draft.Sections) end = std::max(end, section.End);
            m_Draft.Sections.push_back({{}, end, end + 1.f, {}});
            m_Dirty = true;
        }
    }

    //========== 混合、动作通知与目标对齐 ==========

    static bool DrawBlend(const char* title, MontageBlend& blend)
    {
        ImGui::PushID(title);
        ImGui::TextUnformatted(title);
        bool changed = ImGui::InputFloat("时长（秒）", &blend.Seconds, 0, 0, "%.3f");
        changed |= ImGui::SliderFloat2("曲线控制点", &blend.Curve.x, 0, 1);
        float weights[65];
        for (int i = 0; i < IM_ARRAYSIZE(weights); ++i)
            weights[i] = blend.Weight((float)i / (IM_ARRAYSIZE(weights) - 1));
        ImGui::PlotLines("##曲线", weights, IM_ARRAYSIZE(weights), 0, nullptr, 0, 1, {-1, 65});
        ImGui::PopID();
        return changed;
    }

    void AnimationMontagePanel::DrawEvents(const ModelAnimation* model)
    {
        ImGui::TextWrapped("时刻通知一次，例如手碰到工具。区间会在开始和结束各通知一次，例如一段可接受输入的时间。");
        if (model && m_Preview >= 0)
        {
            const AnimationMarker& marker = m_Draft.Events[m_Preview];
            DrawAnimationPose(*model, m_SavedClip, m_SavedEvents, m_Draft.Clip, marker, m_PreviewEnd);
            if (ImGui::SmallButton("收起姿势对照")) m_Preview = -1;
        }
        for (int i = 0; i < (int)m_Draft.Events.size();)
        {
            ImGui::PushID(i);
            AnimationMarker& marker = m_Draft.Events[i];
            m_Dirty |= AnimationFields::EditMarker(marker);
            if (model)
            {
                if (ImGui::SmallButton(marker.End ? "查看开始姿势" : "查看姿势")) { m_Preview = i; m_PreviewEnd = false; }
                ImGui::SameLine();
                if (marker.End)
                {
                    if (ImGui::SmallButton("查看结束姿势")) { m_Preview = i; m_PreviewEnd = true; }
                    ImGui::SameLine();
                }
            }
            bool remove = ImGui::SmallButton("移除通知");
            ImGui::PopID();
            ImGui::Separator();
            if (remove) { m_Draft.Events.erase(m_Draft.Events.begin() + i); m_Preview = -1; m_Dirty = true; }
            else ++i;
        }
        if (ImGui::Button("添加时刻")) { m_Draft.Events.push_back({}); m_Dirty = true; }
        ImGui::SameLine();
        if (ImGui::Button("添加区间")) { m_Draft.Events.push_back({{}, 0, .1f}); m_Dirty = true; }
    }

    void AnimationMontagePanel::DrawWarps()
    {
        ImGui::TextWrapped("游戏在播放时按名称传入目标位置。下列时间段内，角色原片里的根骨移动会向该位置对齐。");
        for (int i = 0; i < (int)m_Draft.WarpWindows.size();)
        {
            MontageWarp& warp = m_Draft.WarpWindows[i];
            ImGui::PushID(i);
            m_Dirty |= Edit("目标名称", warp.Target);
            m_Dirty |= Edit("运动根骨", warp.Root);
            m_Dirty |= ImGui::InputFloat("开始（秒）", &warp.Start, 0, 0, "%.3f");
            m_Dirty |= ImGui::InputFloat("结束（秒）", &warp.End, 0, 0, "%.3f");
            bool remove = ImGui::SmallButton("移除对齐区间");
            ImGui::PopID();
            ImGui::Separator();
            if (remove) { m_Draft.WarpWindows.erase(m_Draft.WarpWindows.begin() + i); m_Dirty = true; }
            else ++i;
        }
        if (ImGui::Button("添加对齐区间")) { m_Draft.WarpWindows.push_back({}); m_Dirty = true; }
    }

    void AnimationMontagePanel::Draw(Entity selected)
    {
        if (!m_Open) return;
        ImGui::SetNextWindowSize({660, 530}, ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("动画蒙太奇", &m_Open)) { ImGui::End(); return; }

        const ModelAnimation* model = nullptr;
        if (selected && selected.HasComponent<ModelRendererComponent>())
        {
            const Ref<Model>& asset = selected.GetComponent<ModelRendererComponent>().ModelAsset;
            if (asset) model = asset->GetAnimation().get();
        }
        ImGui::BeginDisabled(m_Dirty);
        if (ImGui::Button("新建蒙太奇")) New(model);
        ImGui::SameLine();
        if (ImGui::Button("打开文件…"))
        {
            const std::string path = FileDialogs::OpenFile("Animation Montage (*.montage.yaml)\0*.montage.yaml\0");
            if (!path.empty()) Open(fs::path(path));
        }
        ImGui::EndDisabled();
        if (m_Editing)
        {
            ImGui::SameLine();
            if (ImGui::Button("保存")) Save();
            ImGui::SameLine();
            if (ImGui::Button(m_Dirty ? "撤销本次编辑" : "重新载入"))
            {
                m_Dirty = false;
                if (m_Path.empty()) { m_Editing = false; m_Message.clear(); }
                else Open(m_Path);
            }
        }
        if (!m_Message.empty()) ImGui::TextWrapped("%s", m_Message.c_str());
        if (m_Editing)
        {
            if (m_Path.empty()) Edit("保存位置", m_NewFile);
            else ImGui::TextDisabled("%s", m_Path.generic_u8string().c_str());
            if (ImGui::BeginTabBar("蒙太奇内容"))
            {
                if (ImGui::BeginTabItem("原片与骨骼")) { DrawClip(model); ImGui::EndTabItem(); }
                if (ImGui::BeginTabItem("播放段")) { DrawSections(); ImGui::EndTabItem(); }
                if (ImGui::BeginTabItem("淡入淡出"))
                {
                    ImGui::TextWrapped("横轴表示经过的时间。淡入时沿曲线加入姿势，淡出时沿曲线移除姿势。");
                    m_Dirty |= DrawBlend("淡入", m_Draft.BlendIn);
                    m_Dirty |= DrawBlend("淡出", m_Draft.BlendOut);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("动作通知")) { DrawEvents(model); ImGui::EndTabItem(); }
                if (ImGui::BeginTabItem("目标对齐")) { DrawWarps(); ImGui::EndTabItem(); }
                ImGui::EndTabBar();
            }
        }
        ImGui::End();
    }
}
