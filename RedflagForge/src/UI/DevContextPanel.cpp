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
        if (m_Project.empty())
        {
            const char* project = std::getenv("REDFLAG_PROJECT");
            m_Project = project && *project ? std::filesystem::absolute(std::filesystem::u8path(project)).u8string() :
                std::filesystem::current_path().u8string();
        }
        input["project"] = m_Project;
        if (operation == "find" || operation == "show")
        {
            input["query"] = query;
            if (m_Attach) input["task"] = m_Task;
        }
        else if (operation != "tasks") input["task"] = m_Task;
        if (operation == "note") input["text"] = std::string(m_Correction);
        if (operation == "read" || operation == "locate")
        {
            input["kind"] = m_Kind;
            input["file"] = m_File;
            input["first"] = m_First;
        }
        m_Job = std::async(std::launch::async, RunForgeTool, "forge_context.py", input);
    }

    void DevContextPanel::Receive()
    {
        if (!m_Job.valid() || m_Job.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        const YAML::Node reply = m_Job.get();
        if (reply["error"]) m_Error = reply["error"].as<std::string>();
        else if (m_Operation == "tasks") m_Tasks.reset(reply["tasks"]);
        else if (m_Operation == "review") m_Record.reset(reply);
        else if (m_Operation == "read") m_Document.reset(reply);
        else if (m_Operation == "locate") return;
        else if (m_Operation == "note")
        {
            m_Correction[0] = 0;
            Start("review");
        }
        else m_Code.reset(reply);
    }

    // ---- 源码、测试与任务证据 ----

    void DevContextPanel::Read(const std::string& kind, const std::string& file)
    {
        m_Kind = kind;
        m_File = file;
        m_First = 0;
        Start("read");
    }

    void DevContextPanel::DrawDocument()
    {
        if (ImGui::Button("返回资料"))
        {
            m_Document.reset();
            if (m_RecordPage && !m_Record.IsMap()) Start("review");
            return;
        }
        ImGui::SameLine();
        if (ImGui::Button("在文件夹中定位")) Start("locate");
        ImGui::SameLine();
        const std::string path = m_Document["path"].Scalar();
        if (ImGui::Button("复制完整路径")) ImGui::SetClipboardText(path.c_str());
        ImGui::TextWrapped("%s", path.c_str());
        ImGui::Separator();
        const int first = m_Document["first"].as<int>(), total = m_Document["total"].as<int>();
        const int page = m_Document["page"].as<int>(), focus = m_Document["focus"].as<int>();
        if (total == 0) { ImGui::TextDisabled("没有内容。"); return; }
        ImGui::Text("%s    第 %d–%d 行，共 %d 行", m_Document["title"].Scalar().c_str(),
                    first, std::min(first + page - 1, total), total);
        auto turn = [&](const char* label, int line, bool enabled) {
            ImGui::BeginDisabled(!enabled);
            if (ImGui::SmallButton(label)) { m_First = line; Start("read"); }
            ImGui::EndDisabled();
        };
        turn("上一页", first - page, first > 1);
        ImGui::SameLine();
        turn("下一页", first + page, first + page <= total);
        ImGui::BeginChild("##文件正文", {0,0}, false, ImGuiWindowFlags_HorizontalScrollbar);
        int number = first;
        const bool diff = m_Kind == "changes" || std::filesystem::u8path(m_File).extension() == ".diff";
        for (const auto& line : m_Document["lines"])
        {
            const std::string text = line.Scalar();
            ImGui::TextDisabled("%4d", number);
            ImGui::SameLine();
            ImVec4 color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
            if (diff && text.rfind("+", 0) == 0) color = {.62f,.85f,.64f,1};
            else if (diff && text.rfind("-", 0) == 0) color = {1,.66f,.62f,1};
            else if (m_Kind == "source" && number == focus) color = {1,.82f,.46f,1};
            ImGui::TextColored(color,"%s",text.c_str());
            if (m_Kind == "source" && number == focus && ImGui::IsWindowAppearing()) ImGui::SetScrollHereY(.35f);
            ++number;
        }
        ImGui::EndChild();
    }

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
        if ((ImGui::Button("查找") || enter) && m_Search[0])
        {
            const std::string query = m_Search;
            const size_t colon = query.rfind(':');
            const bool line = colon != std::string::npos && colon + 1 < query.size() &&
                query.find_first_not_of("0123456789", colon + 1) == std::string::npos;
            Start(line ? "show" : "find", query);
        }
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
                if (ImGui::SmallButton(source.c_str())) Read("source", source);
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
        if (ImGui::SmallButton("打开源码原文")) Read("source", source);
        ImGui::SameLine();
        ImGui::TextWrapped("%s", source.c_str());
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
            {
                ImGui::TextDisabled("%s", item.second["changed"].as<bool>() ? "已修改" : "与开始时相同");
                ImGui::SameLine();
                const std::string file = item.first.Scalar();
                if (ImGui::SmallButton(file.c_str())) Read("source", file);
            }
            if (m_Record["diff_preview"].size() && ImGui::SmallButton("查看当前完整改动")) Read("changes");
        }
        if (ImGui::CollapsingHeader("验证", ImGuiTreeNodeFlags_DefaultOpen))
            for (const YAML::Node& item : m_Record["checks"])
            {
                const std::string log = item["log"].Scalar();
                ImGui::PushID(log.c_str());
                const int exit = item["exit_code"].as<int>();
                ImGui::TextColored(exit ? ImVec4{1,.66f,.62f,1} : ImVec4{.62f,.85f,.64f,1},
                                   "%s  ·  退出码 %d", exit ? "测试失败" : "测试通过", exit);
                ImGui::SameLine();
                ImGui::TextDisabled("%s", item["current"].as<bool>() ? "对应当前源码" : "源码已变化，需复测");
                std::string command;
                for (const YAML::Node& arg : item["command"]) command += (command.empty() ? "" : " ") + arg.Scalar();
                ImGui::TextWrapped("%s", command.c_str());
                if (ImGui::SmallButton("原始测试输出")) Read("artifact", log);
                ImGui::SameLine();
                if (ImGui::SmallButton("测试时的改动")) Read("artifact", item["diff"].Scalar());
                ImGui::Separator();
                ImGui::PopID();
            }
        if (m_Record["references"].size() && ImGui::CollapsingHeader("引用", ImGuiTreeNodeFlags_DefaultOpen))
            for (const YAML::Node& item : m_Record["references"])
            {
                ImGui::PushID(item["evidence"].Scalar().c_str());
                ImGui::TextWrapped("%s  %s", item["operation"].Scalar().c_str(), item["query"].Scalar().c_str());
                for (const YAML::Node& source : item["sources"])
                    if (ImGui::SmallButton(source.Scalar().c_str())) Read("source", source.Scalar());
                if (ImGui::SmallButton("当时的查询记录")) Read("artifact", item["evidence"].Scalar());
                ImGui::PopID();
            }
        if (ImGui::Button("打开证据目录")) { m_Kind = "folder"; m_File.clear(); Start("locate"); }
    }

    void DevContextPanel::DrawProject()
    {
        if (ImGui::SmallButton("切换项目"))
        {
            snprintf(m_ProjectInput, sizeof(m_ProjectInput), "%s", m_Project.c_str());
            ImGui::OpenPopup("选择资料项目");
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", m_Project.c_str());
        if (!ImGui::BeginPopup("选择资料项目")) return;
        ImGui::TextUnformatted("项目目录");
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 28);
        const bool enter = ImGui::InputText("##项目目录", m_ProjectInput, sizeof(m_ProjectInput), ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::Button("打开项目") || enter)
        {
            std::error_code error;
            const auto path = std::filesystem::u8path(m_ProjectInput);
            if (std::filesystem::is_directory(path, error))
            {
                m_Project = std::filesystem::absolute(path).lexically_normal().u8string();
                m_Task.clear();
                m_Attach = false;
                m_Correction[0] = 0;
                m_Code.reset(); m_Tasks.reset(); m_Record.reset(); m_Document.reset();
                ImGui::CloseCurrentPopup();
                Start("tasks");
            }
            else m_Error = "找不到这个项目目录。";
        }
        ImGui::EndPopup();
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
            const char* artifact = std::getenv("REDFLAG_DEV_EVIDENCE");
            if (task && *task) m_Task = task;
            if (symbol && *symbol)
            {
                m_Open = true;
                Start("show", symbol);
            }
            else if (!m_Task.empty())
            {
                m_Open = m_RecordPage = true;
                if (artifact && *artifact) Read("artifact", artifact); else Start("review");
            }
        }
        Receive();
        if (!m_Open) return;
        ImGui::SetNextWindowSize({740, 570}, ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("开发资料", &m_Open)) { ImGui::End(); return; }
        ImGui::BeginDisabled(m_Job.valid());
        DrawProject();
        if (m_Document.IsMap()) DrawDocument(); else DrawBrowse();
        ImGui::EndDisabled();
        if (m_Job.valid()) ImGui::TextDisabled("正在读取项目资料…");
        if (!m_Error.empty()) ImGui::TextWrapped("%s", m_Error.c_str());
        ImGui::End();
    }

    void DevContextPanel::DrawBrowse()
    {
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
    }
}
