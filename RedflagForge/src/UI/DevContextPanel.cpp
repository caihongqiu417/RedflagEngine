#include "rfpch.h"
#include "DevContextPanel.h"
#include "../ForgeTool.h"
#include <filesystem>
#include <imgui.h>

namespace Redflag
{
    // ---- 后台查询与校正 ----

    void DevContextPanel::Start(const std::string& operation, const std::string& query)
    {
        m_Operation = operation;
        m_Error.clear();
        YAML::Node input;
        input["operation"] = operation;
        const char* project = std::getenv("REDFLAG_PROJECT");
        input["project"] = project && *project ? std::filesystem::absolute(std::filesystem::u8path(project)).u8string() :
            std::filesystem::current_path().u8string();
        if (operation == "find" || operation == "show")
        {
            input["query"] = query;
            if (m_Attach) input["task"] = m_Task;
        }
        else if (operation != "tasks") input["task"] = m_Task;
        if (operation == "note") input["text"] = std::string(m_Correction);
        m_Job = std::async(std::launch::async, RunForgeTool, "forge_context.py", input);
    }

    void DevContextPanel::Receive()
    {
        if (!m_Job.valid() || m_Job.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        const YAML::Node reply = m_Job.get();
        if (reply["error"]) m_Error = reply["error"].as<std::string>();
        else if (m_Operation == "tasks") m_Tasks.reset(reply["tasks"]);
        else if (m_Operation == "review") m_Record.reset(reply);
        else if (m_Operation == "note")
        {
            m_Correction[0] = 0;
            Start("review");
        }
        else m_Code.reset(reply);
    }

    // ---- 源码、测试与任务证据 ----

    static void Details(const YAML::Node& entries)
    {
        for (const YAML::Node& item : entries) ImGui::TextWrapped("%s", item.Scalar().c_str());
    }

    void DevContextPanel::DrawCode()
    {
        ImGui::SetNextItemWidth(-ImGui::GetFontSize() * 4.5f);
        const bool enter = ImGui::InputTextWithHint("##源码查询", "输入接口名、文件位置或关键词", m_Search, sizeof(m_Search),
                                                     ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if ((ImGui::Button("查找") || enter) && m_Search[0]) Start("find", m_Search);
        if (!m_Task.empty()) ImGui::Checkbox("把这次查询记入选中的任务", &m_Attach);
        if (!m_Code.IsMap()) return;
        if (const auto matches = m_Code["matches"])
        {
            if (!matches.size()) ImGui::TextDisabled("没有找到匹配的接口。");
            int i = 0;
            for (const YAML::Node& item : matches)
            {
                const std::string source = item["source"].as<std::string>();
                ImGui::PushID(i++);
                if (ImGui::Selectable(item["symbol"].as<std::string>().c_str())) Start("show", source);
                ImGui::TextWrapped("%s", source.c_str());
                const std::string summary = item["summary"].as<std::string>("");
                if (!summary.empty()) ImGui::TextWrapped("%s", summary.c_str());
                ImGui::Separator();
                ImGui::PopID();
            }
            const size_t more = m_Code["more"].as<size_t>(0);
            if (more) ImGui::TextDisabled("另有 %zu 项匹配，可以增加关键词缩小范围。", more);
            return;
        }
        if (!m_Code["symbol"]) return;
        const std::string source = m_Code["source"].as<std::string>();
        ImGui::TextWrapped("%s", source.c_str());
        if (ImGui::SmallButton("复制源码位置")) ImGui::SetClipboardText(source.c_str());
        if (const auto lines = m_Code["lines"])
        {
            for (const YAML::Node& line : lines) ImGui::TextWrapped("%d   %s", line["line"].as<int>(), line["text"].Scalar().c_str());
            return;
        }
        if (m_Code["index_current"] && !m_Code["index_current"].as<bool>())
        {
            ImGui::TextWrapped("%s", m_Code["message"].as<std::string>().c_str());
            return;
        }
        ImGui::TextWrapped("%s", m_Code["signature"].as<std::string>("").c_str());
        ImGui::TextWrapped("%s", m_Code["summary"].as<std::string>("").c_str());
        auto sites = [&](const char* title, const YAML::Node& rows) {
            if (!rows.size() || !ImGui::CollapsingHeader(title, ImGuiTreeNodeFlags_DefaultOpen)) return;
            ImGui::PushID(title);
            int index = 0;
            for (const YAML::Node& row : rows)
            {
                ImGui::PushID(index++);
                const std::string at = row["source"].Scalar();
                if (ImGui::Selectable(at.c_str())) Start("show", at);
                ImGui::TextWrapped("%s", row["text"].Scalar().c_str());
                ImGui::PopID();
            }
            ImGui::PopID();
        };
        sites("实现", m_Code["implementation"]);
        sites("可能的调用位置", m_Code["references"]);
        sites("测试", m_Code["tests"]);
        sites("测试脚本位置", m_Code["runners"]);
    }

    void DevContextPanel::DrawRecord()
    {
        if (m_Task.empty()) { ImGui::TextDisabled("先选择要查阅的任务。"); return; }
        if (!m_Record.IsMap() || m_Record["task"].as<std::string>("") != m_Task) return;
        ImGui::TextWrapped("%s", m_Record["intent"].as<std::string>("").c_str());
        ImGui::InputTextMultiline("##更正", m_Correction, sizeof(m_Correction), {-1, ImGui::GetTextLineHeight() * 2.5f});
        if (ImGui::Button("记下更正") && m_Correction[0]) Start("note");
        ImGui::SameLine();
        if (ImGui::Button("重新读取记录")) Start("review");
        if (m_Record["notes"].size() && ImGui::CollapsingHeader("人工记录", ImGuiTreeNodeFlags_DefaultOpen))
            Details(m_Record["notes"]);
        if (ImGui::CollapsingHeader("文件修改", ImGuiTreeNodeFlags_DefaultOpen))
        {
            for (const auto& item : m_Record["files"])
                ImGui::TextWrapped("%s   %s", item.second["changed"].as<bool>() ? "已修改" : "与开始时相同", item.first.Scalar().c_str());
            Details(m_Record["diff_preview"]);
        }
        if (ImGui::CollapsingHeader("引用", ImGuiTreeNodeFlags_DefaultOpen))
            for (const YAML::Node& item : m_Record["references"])
            {
                ImGui::TextWrapped("%s  %s", item["operation"].Scalar().c_str(), item["query"].Scalar().c_str());
                Details(item["sources"]);
            }
        if (ImGui::CollapsingHeader("验证", ImGuiTreeNodeFlags_DefaultOpen))
            for (const YAML::Node& item : m_Record["checks"])
            {
                std::string command;
                for (const YAML::Node& arg : item["command"]) command += (command.empty() ? "" : " ") + arg.Scalar();
                ImGui::TextWrapped("%s", command.c_str());
                ImGui::Text("退出码 %d   %s", item["exit_code"].as<int>(), item["current"].as<bool>() ? "对应当前源码" : "源码已变化，需复测");
                ImGui::TextWrapped("%s", item["log"].Scalar().c_str());
            }
        if (m_Record["evidence"].IsScalar() && ImGui::SmallButton("复制证据目录"))
            ImGui::SetClipboardText(m_Record["evidence"].Scalar().c_str());
    }

    void DevContextPanel::DrawMenu()
    {
        if (ImGui::MenuItem("开发资料", nullptr, &m_Open) && m_Open && !m_Job.valid()) Start("tasks");
    }

    void DevContextPanel::Draw()
    {
        if (m_Startup)
        {
            m_Startup = false;
            const char* task = std::getenv("REDFLAG_DEV_TASK");
            const char* symbol = std::getenv("REDFLAG_DEV_SYMBOL");
            if (task && *task) m_Task = task;
            if (symbol && *symbol)
            {
                m_Open = true;
                Start("show", symbol);
            }
            else if (!m_Task.empty())
            {
                m_Open = m_RecordPage = true;
                Start("review");
            }
        }
        Receive();
        if (!m_Open) return;
        ImGui::SetNextWindowSize({740, 570}, ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("开发资料", &m_Open)) { ImGui::End(); return; }
        ImGui::BeginDisabled(m_Job.valid());
        if (ImGui::BeginCombo("任务", m_Task.empty() ? "选择任务" : m_Task.c_str()))
        {
            for (const YAML::Node& item : m_Tasks)
            {
                const std::string task = item["task"].as<std::string>();
                const std::string label = item["intent"].as<std::string>() + "   " + task;
                if (ImGui::Selectable(label.c_str(), m_Task == task))
                {
                    m_Task = task;
                    m_Correction[0] = 0;
                    Start("review");
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::SmallButton("刷新任务列表")) Start("tasks");
        ImGui::Separator();
        if (ImGui::RadioButton("源码与测试", !m_RecordPage)) m_RecordPage = false;
        ImGui::SameLine();
        if (ImGui::RadioButton("任务记录", m_RecordPage)) m_RecordPage = true;
        if (m_RecordPage) DrawRecord(); else DrawCode();
        ImGui::EndDisabled();
        if (m_Job.valid()) ImGui::TextDisabled("正在读取项目资料…");
        if (!m_Error.empty()) ImGui::TextWrapped("%s", m_Error.c_str());
        ImGui::End();
    }
}
