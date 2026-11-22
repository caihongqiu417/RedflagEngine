#include "rfpch.h"
#include "NpcAuthoringPanel.h"
#include "NpcAuthoringFiles.h"
#include "AnimationPoseView.h"
#include <imgui.h>
#include <mmsystem.h>

#pragma comment(lib, "winmm.lib")

namespace Redflag
{
    namespace fs = std::filesystem;

    static bool Edit(const char* label, std::string& value, bool multiline = false)
    {
        auto resize = [](ImGuiInputTextCallbackData* event) {
            std::string& text = *static_cast<std::string*>(event->UserData);
            text.resize(event->BufTextLen);
            event->Buf = text.data();
            return 0;
        };
        if (multiline)
            return ImGui::InputTextMultiline(label, value.data(), value.capacity() + 1,
                                             {-1, ImGui::GetTextLineHeight() * 4}, ImGuiInputTextFlags_CallbackResize,
                                             resize, &value);
        return ImGui::InputText(label, value.data(), value.capacity() + 1, ImGuiInputTextFlags_CallbackResize, resize,
                                &value);
    }

    static YAML::Node Profile(const NpcDefinition& npc)
    {
        YAML::Node data;
        data["Name"] = npc.Name;
        data["Instructions"] = npc.Instructions;
        data["Voice"] = npc.Voice;
        data["Model"] = npc.Model;
        data["IdleClip"] = npc.IdleClip;
        data["WalkClip"] = npc.WalkClip;
        data["Remember"] = npc.Remember;
        data["ObserveWorld"] = npc.ObserveWorld;
        data["ObservePictures"] = npc.Memory.Pictures;
        data["Sight"] = npc.Sight;
        YAML::Node rows(YAML::NodeType::Sequence);
        for (const NpcIntent& intent : npc.Intents)
        {
            YAML::Node row;
            row["Key"] = intent.Key;
            row["Description"] = intent.Description;
            row["Target"] = intent.Target;
            if (!intent.Item.empty())
                row["Item"] = intent.Item;
            rows.push_back(row);
        }
        data["Intents"] = rows;
        return data;
    }

    // ---- 打开角色与后台保存 ----

    void NpcAuthoringPanel::Open(const fs::path& path)
    {
        if (m_Path != path)
        {
            StopAudio();
            m_PreviewEvent = -1;
        }
        m_Path = path;
        m_Message.clear();
        m_Loaded = !path.empty() && m_Saved.Load(path, m_Message);
        if (m_Loaded)
        {
            m_Draft = m_Saved;
            m_States = m_Saved.Controller.GetStates();
            if (!m_Saved.Controller.Find(m_EventState) && !m_States.empty())
            {
                const auto marked = std::find_if(m_States.begin(), m_States.end(),
                    [](const AnimationState& state) { return !state.Events.empty(); });
                m_EventState = (marked == m_States.end() ? m_States.front() : *marked).ID;
            }
        }
    }

    void NpcAuthoringPanel::Start(YAML::Node request)
    {
        request["profile"] = fs::absolute(m_Path).u8string();
        m_TaskOwner = m_Path;
        m_Operation = request["operation"].as<std::string>();
        m_Task = std::async(std::launch::async, RunNpcAuthoringTool, request);
        m_Message = "正在处理…";
    }

    bool NpcAuthoringPanel::Draw(std::string& definitionPath, const BehaviorTrace* trace, const std::string& stage,
                                 const std::vector<std::string>& modelClips, const ModelAnimation* model)
    {
        bool changed = false;
        const fs::path selected = fs::u8path(definitionPath);
        if (m_Path != selected)
            Open(selected);

        if (m_Task.valid() && m_Task.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            YAML::Node reply = m_Task.get();
            if (m_TaskOwner != selected)
                m_Message.clear();
            else if (reply["error"])
                m_Message = reply["error"].as<std::string>();
            else if (m_Operation == "voices")
            {
                m_Voices = reply["voices"].as<std::vector<std::string>>();
                m_DefaultVoice = reply["default_voice"].as<std::string>("");
                m_Message = "已读取本机语音服务的可用音色。";
            }
            else if (m_Operation == "speak")
            {
                if (m_PlayOnReady && PlaySoundW(m_Audio.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT))
                    m_Message = "正在试听。";
                else
                    m_Message = m_PlayOnReady ? "声音设备无法播放这个 WAV 文件。" : "已停止试听。";
            }
            else if (m_Operation == "export")
                m_Message = "已导出表格。";
            else
            {
                const fs::path saved = fs::u8path(reply["profile"].as<std::string>());
                if (m_Operation == "copy")
                    definitionPath = (selected.parent_path() / saved.filename()).generic_u8string();
                Open(fs::u8path(definitionPath));
                changed = m_Loaded;
                if (m_Loaded)
                    m_Message = "已保存并重新读入角色。";
            }
        }

        if (m_Request)
        {
            if (std::optional<DecisionResult> result = m_Client.Take(m_Request))
            {
                m_Request = 0;
                if (m_RequestOwner != selected)
                    m_Message.clear();
                else if (result->Status == DecisionStatus::Completed)
                {
                    BehaviorBlackboard actor;
                    const BehaviorTrace preview = m_Draft.Tree.Tick(actor, result->Choice);
                    m_Message = "模型选择 " + result->Choice + "，行为树动作 " + preview.Action;
                }
                else
                    m_Message = result->Status == DecisionStatus::Cancelled ? "已停止试答。" : result->Error;
            }
        }

        ImGui::SetNextWindowSize({640, 600}, ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("角色创作"))
        {
            ImGui::End();
            return changed;
        }
        ImGui::BeginDisabled(m_Task.valid());
        fs::path directory = selected.empty() ? fs::path("assets/ai") : selected.parent_path();
        if (ImGui::BeginCombo("角色资产", m_Path.filename().u8string().c_str()))
        {
            std::error_code error;
            for (const fs::directory_entry& file : fs::directory_iterator(directory, error))
            {
                std::string name = file.path().filename().u8string();
                if (name.size() <= 9 || name.compare(name.size() - 9, 9, ".npc.yaml") != 0)
                    continue;
                if (ImGui::Selectable(name.c_str(), file.path() == m_Path))
                {
                    Open(file.path());
                    if (m_Loaded)
                    {
                        definitionPath = file.path().generic_u8string();
                        changed = true;
                    }
                }
            }
            ImGui::EndCombo();
        }
        if (m_Loaded)
        {
            if (ImGui::Button("保存角色"))
            {
                YAML::Node request, before = Profile(m_Saved), after = Profile(m_Draft);
                request["operation"] = "save";
                request["changes"] = YAML::Node(YAML::NodeType::Map);
                request["original"] = YAML::Node(YAML::NodeType::Map);
                for (const auto& entry : after)
                {
                    const std::string key = entry.first.as<std::string>();
                    if (YAML::Dump(before[key]) == YAML::Dump(entry.second))
                        continue;
                    request["changes"][key] = entry.second;
                    request["original"][key] = before[key];
                }
                Start(request);
            }
            ImGui::SameLine();
            if (ImGui::Button("重新读取"))
                Open(selected);
            ImGui::SameLine();
            if (ImGui::Button("复制角色…"))
            {
                m_CopyName = m_Draft.Name;
                m_CopyFile.clear();
                ImGui::OpenPopup("复制角色");
            }
            if (ImGui::BeginPopup("复制角色"))
            {
                Edit("新角色名称", m_CopyName);
                Edit("新文件名", m_CopyFile);
                ImGui::TextDisabled("例如 medic.npc.yaml，副本会有自己的行为树。");
                if (ImGui::Button("创建副本"))
                {
                    YAML::Node request;
                    request["operation"] = "copy";
                    request["name"] = m_CopyName;
                    request["source"] = m_Path.filename().u8string();
                    request["file"] = m_CopyFile;
                    Start(request);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            if (ImGui::BeginTabBar("创作"))
            {
                if (ImGui::BeginTabItem("角色"))
                {
                    DrawProfile();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("候选"))
                {
                    DrawIntents();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("动画"))
                {
                    DrawAnimations(stage, modelClips);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("动画事件"))
                {
                    DrawAnimationEvents(model);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("表格"))
                {
                    DrawTables();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("试答与试听"))
                {
                    DrawPreview();
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }
        ImGui::EndDisabled();
        if (m_PlayOnReady && ImGui::Button("停止试听"))
            StopAudio();
        if (!stage.empty())
            ImGui::TextWrapped("当前动作阶段  %s", stage.c_str());
        if (trace && !trace->Nodes.empty())
        {
            std::string path;
            for (uint32_t id : trace->Nodes)
                path += (path.empty() ? "" : " → ") + std::to_string(id);
            ImGui::TextWrapped("本次执行  %s   %s", path.c_str(), trace->Action.c_str());
        }
        if (!m_Message.empty())
            ImGui::TextWrapped("%s", m_Message.c_str());
        ImGui::End();
        return changed;
    }

    // ---- 性格、动画、记忆与观察 ----

    void NpcAuthoringPanel::DrawProfile()
    {
        Edit("名称", m_Draft.Name);
        ImGui::TextUnformatted("性格及行动约定");
        Edit("##Instructions", m_Draft.Instructions, true);
        Edit("决策模型", m_Draft.Model);
        if (m_Draft.Model.empty())
            ImGui::TextDisabled("留空使用决策服务的默认模型。");
        Edit("音色", m_Draft.Voice);
        if (m_Draft.Voice.empty())
        {
            if (m_DefaultVoice.empty())
                ImGui::TextDisabled("使用系统默认音色。读取可用音色可以查看名称。");
            else
                ImGui::Text("系统默认音色  %s", m_DefaultVoice.c_str());
        }
        if (ImGui::Button("读取可用音色"))
        {
            YAML::Node request;
            request["operation"] = "voices";
            Start(request);
        }
        if (!m_Voices.empty() && ImGui::BeginCombo("本机音色", m_Draft.Voice.c_str()))
        {
            for (const std::string& voice : m_Voices)
                if (ImGui::Selectable(voice.c_str(), voice == m_Draft.Voice))
                    m_Draft.Voice = voice;
            ImGui::EndCombo();
        }
        ImGui::Separator();
        Edit("待机动画", m_Draft.IdleClip);
        Edit("行走动画", m_Draft.WalkClip);
        ImGui::Checkbox("提问时带上角色记下的事", &m_Draft.Remember);
        ImGui::Checkbox("观察场景中的物体", &m_Draft.ObserveWorld);
        ImGui::Checkbox("允许用角色相机拍照", &m_Draft.Memory.Pictures);
        ImGui::InputFloat("观察距离（米）", &m_Draft.Sight);
    }

    // ---- 候选行与电子表格 ----

    void NpcAuthoringPanel::DrawIntents()
    {
        if (ImGui::Button("新增候选"))
            m_Draft.Intents.emplace_back();
        if (!ImGui::BeginTable("候选表", 5, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_Resizable))
            return;
        for (const char* column : {"动作键", "交给模型的说明", "目标来源", "物品", "操作"})
            ImGui::TableSetupColumn(column);
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < m_Draft.Intents.size(); ++i)
        {
            NpcIntent& row = m_Draft.Intents[i];
            ImGui::PushID(int(i));
            ImGui::TableNextRow();
            for (std::string* cell : {&row.Key, &row.Description, &row.Target, &row.Item})
            {
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1);
                ImGui::PushID(cell);
                Edit("##字段", *cell);
                ImGui::PopID();
            }
            ImGui::TableNextColumn();
            const bool remove = ImGui::SmallButton("删除");
            ImGui::PopID();
            if (remove)
            {
                m_Draft.Intents.erase(m_Draft.Intents.begin() + i);
                break;
            }
        }
        ImGui::EndTable();
        ImGui::TextWrapped("候选键连接行为树。目标来源决定游戏去哪里找对象，例如 pickup 找可拿的物品，leader 找队友。");
    }

    void NpcAuthoringPanel::DrawTables()
    {
        ImGui::Combo("表格内容", &m_Table, "角色属性\0行为树连线\0动作候选\0");
        const char* tables[] = {"profile", "links", "intents"};
        for (bool import : {false, true})
        {
            if (import)
                ImGui::SameLine();
            if (!ImGui::Button(import ? "导入表格…" : "导出表格…"))
                continue;
            const std::filesystem::path file = ChooseNpcTable(!import);
            if (file.empty())
                continue;
            YAML::Node request;
            request["operation"] = import ? "apply" : "export";
            request["table"] = tables[m_Table];
            request["file"] = file.u8string();
            Start(request);
        }
        ImGui::TextWrapped("表格从磁盘上的角色导出。在「改成」列填写新值，再导入这里。");
    }

    // ---- 游戏阶段与动画剪辑 ----

    static YAML::Node WriteEvents(const std::vector<AnimationMarker>& events)
    {
        YAML::Node list(YAML::NodeType::Sequence);
        for (const AnimationMarker& event : events)
        {
            YAML::Node item;
            item["Name"] = event.Name;
            item["Start"] = event.Start;
            if (event.End)
                item["End"] = *event.End;
            list.push_back(item);
        }
        return list;
    }

    void NpcAuthoringPanel::DrawAnimationEvents(const ModelAnimation* model)
    {
        if (m_States.empty())
        {
            ImGui::TextWrapped("请先在「动画」页选择一张动作表。");
            return;
        }
        if (ImGui::BeginCombo("动作", m_EventState.c_str()))
        {
            for (const AnimationState& row : m_States)
                if (ImGui::Selectable(row.ID.c_str(), row.ID == m_EventState))
                {
                    m_EventState = row.ID;
                    m_PreviewEvent = -1;
                }
            ImGui::EndCombo();
        }
        AnimationState& state = *std::find_if(m_States.begin(), m_States.end(),
            [&](const AnimationState& row) { return row.ID == m_EventState; });
        ImGui::Text("剪辑   %s", state.Clip.c_str());
        ImGui::TextWrapped("时间从剪辑开头算起。填一个时刻会通知脚本一次，填一段时间会分别通知开始和结束。");
        if (model && m_PreviewEvent >= 0 && m_PreviewEvent < (int)state.Events.size())
        {
            const AnimationMarker& event = state.Events[m_PreviewEvent];
            DrawAnimationPose(*model, *m_Saved.Controller.Find(state.ID), state, event, m_PreviewEnd && event.End.has_value());
            if (ImGui::SmallButton("收起姿势对照"))
                m_PreviewEvent = -1;
        }
        ImGui::Separator();
        ImGui::PushID(state.ID.c_str());

        for (int i = 0; i < (int)state.Events.size();)
        {
            AnimationMarker& event = state.Events[i];
            ImGui::PushID(i);
            Edit("事件名称", event.Name);
            ImGui::InputFloat(event.End ? "开始（秒）" : "时刻（秒）", &event.Start, 0, 0, "%.4f");
            if (event.End)
                ImGui::InputFloat("结束（秒）", &*event.End, 0, 0, "%.4f");
            if (model)
            {
                if (ImGui::SmallButton(event.End ? "查看开始姿势" : "查看姿势"))
                {
                    m_PreviewEvent = i;
                    m_PreviewEnd = false;
                }
                if (event.End)
                {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("查看结束姿势"))
                    {
                        m_PreviewEvent = i;
                        m_PreviewEnd = true;
                    }
                }
                ImGui::SameLine();
            }
            const bool remove = ImGui::SmallButton("删除事件");
            ImGui::PopID();
            ImGui::Separator();
            if (remove)
            {
                state.Events.erase(state.Events.begin() + i);
                m_PreviewEvent = -1;
            }
            else
                ++i;
        }

        if (ImGui::Button("添加时刻"))
            state.Events.push_back({"", 0.f, {}});
        ImGui::SameLine();
        if (ImGui::Button("添加区间"))
            state.Events.push_back({"", 0.f, .1f});
        ImGui::PopID();
        SaveAnimations();
    }

    void NpcAuthoringPanel::DrawAnimations(const std::string& stage, const std::vector<std::string>& modelClips)
    {
        const fs::path directory = m_Path.parent_path();
        if (ImGui::BeginCombo("动作表", m_Saved.AnimatorFile.empty()
                                            ? "选择动作表"
                                            : m_Saved.AnimatorFile.filename().u8string().c_str()))
        {
            std::error_code error;
            for (const fs::directory_entry& entry : fs::directory_iterator(directory, error))
            {
                const std::string name = entry.path().filename().u8string();
                if (name.size() <= 14 || name.compare(name.size() - 14, 14, ".animator.yaml") != 0)
                    continue;
                if (ImGui::Selectable(name.c_str(), entry.path() == m_Saved.AnimatorFile))
                {
                    YAML::Node request;
                    request["operation"] = "save";
                    request["original"] = YAML::Node(YAML::NodeType::Map);
                    request["changes"]["Animator"] = name;
                    Start(request);
                }
            }
            ImGui::EndCombo();
        }
        if (m_Saved.AnimatorFile.empty())
        {
            if (ImGui::Button("从待机动画创建动作表"))
            {
                YAML::Node request;
                request["operation"] = "new-animator";
                request["clip"] = m_Draft.IdleClip;
                Start(request);
            }
            return;
        }
        if (ImGui::BeginTable("动作表", 6, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_Resizable))
        {
            for (const char* col : {"状态", "剪辑", "循环", "过渡(秒)", "接触时刻", "时长(秒)"})
                ImGui::TableSetupColumn(col);
            ImGui::TableHeadersRow();
            for (AnimationState& state : m_States)
            {
                ImGui::PushID(state.ID.c_str());
                ImGui::TableNextRow();
                if (state.ID == stage)
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ImGuiCol_Header));
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(state.ID.c_str());
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1);
                if (modelClips.empty())
                    Edit("##clip", state.Clip);
                else if (ImGui::BeginCombo("##clip", state.Clip.c_str()))
                {
                    for (const std::string& clip : modelClips)
                        if (ImGui::Selectable(clip.c_str(), clip == state.Clip))
                            state.Clip = clip;
                    ImGui::EndCombo();
                }
                ImGui::TableNextColumn();
                ImGui::Checkbox("##loop", &state.Loop);
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1);
                ImGui::InputFloat("##blend", &state.Blend, 0, 0, "%.2f");
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1);
                ImGui::InputFloat("##contact", &state.Contact, 0, 0, "%.2f");
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1);
                ImGui::InputFloat("##seconds", &state.Seconds, 0, 0, "%.2f");
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::TextWrapped(
            "接触时刻从 0 到 1。例如填 0.5，角色在动作播到一半时拿起物品。时长为 0 时沿用剪辑本身的长度。");
        SaveAnimations();
    }

    void NpcAuthoringPanel::SaveAnimations()
    {
        if (ImGui::Button("保存动作表"))
        {
            YAML::Node request;
            request["operation"] = "states";
            request["changes"] = YAML::Node(YAML::NodeType::Map);
            request["original"] = YAML::Node(YAML::NodeType::Map);
            for (const AnimationState& row : m_States)
            {
                const AnimationState& old = *m_Saved.Controller.Find(row.ID);
                auto field = [&](const char* name, const auto& before, const auto& after) {
                    if (before != after)
                    {
                        request["changes"][row.ID][name] = after;
                        request["original"][row.ID][name] = before;
                    }
                };
                field("Clip", old.Clip, row.Clip);
                field("Loop", old.Loop, row.Loop);
                field("Blend", old.Blend, row.Blend);
                field("Contact", old.Contact, row.Contact);
                field("Seconds", old.Seconds, row.Seconds);
                if (row.Events != old.Events)
                {
                    request["original"][row.ID]["Events"] = WriteEvents(old.Events);
                    request["changes"][row.ID]["Events"] = WriteEvents(row.Events);
                }
            }
            Start(request);
        }
    }

    // ---- 试答、试听与停止 ----

    void NpcAuthoringPanel::DrawPreview()
    {
        Edit("玩家的话", m_Prompt, true);
        if (!m_Request && ImGui::Button("试答"))
        {
            DecisionRequest request;
            request.Prompt = m_Prompt;
            request.Instructions = m_Draft.Instructions;
            request.Model = m_Draft.Model;
            for (const NpcIntent& intent : m_Draft.Intents)
                request.Candidates.push_back({intent.Key, intent.Description});
            m_Request = m_Client.Start(std::move(request));
            m_RequestOwner = m_Path;
            m_Message = "正在等待模型选择…";
        }
        if (m_Request && ImGui::Button("停止试答"))
            m_Client.Cancel(m_Request);
        ImGui::TextDisabled("试答使用表中的候选，不会移动场景物体。");
        Edit("试听台词", m_Sample);
        if (ImGui::Button("试听"))
        {
            StopAudio();
            if (m_Audio.empty())
                m_Audio = NpcTemporaryFile();
            YAML::Node request;
            request["operation"] = "speak";
            request["voice"] = m_Draft.Voice;
            request["text"] = m_Sample;
            request["audio"] = m_Audio.u8string();
            m_PlayOnReady = true;
            Start(request);
        }
    }

    void NpcAuthoringPanel::StopAudio()
    {
        if (m_PlayOnReady)
            PlaySoundW(nullptr, nullptr, 0);
        m_PlayOnReady = false;
        m_Message = "已停止试听。";
    }

    NpcAuthoringPanel::~NpcAuthoringPanel()
    {
        StopAudio();
        if (m_Task.valid())
            m_Task.wait();
        std::error_code error;
        if (!m_Audio.empty())
            fs::remove(m_Audio, error);
    }
}
