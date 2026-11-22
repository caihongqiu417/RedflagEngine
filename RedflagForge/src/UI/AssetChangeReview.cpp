#include "rfpch.h"
#include "AssetChangeReview.h"
#include "../ForgeTool.h"
#include <imgui.h>

namespace Redflag
{
    namespace fs = std::filesystem;

    static std::string FieldName(const YAML::Node& steps)
    {
        std::string field;
        for (const YAML::Node& step : steps)
        {
            if (!field.empty()) field += " / ";
            if (step.IsScalar())
                field += step.Scalar();
            else if (step.IsMap() && step.size() == 1)
            {
                const auto match = step.begin();
                field += match->first.Scalar() + " = " + match->second.Scalar();
            }
        }
        return field;
    }

    void AssetChangeReview::Select(const std::string& id)
    {
        m_ID = id;
        m_NextPoll = 0;
        m_Message.clear();
    }

    void AssetChangeReview::Refresh()
    {
        const char* project = std::getenv("REDFLAG_PROJECT");
        m_Project = project && *project ? fs::absolute(fs::u8path(project)) : fs::current_path();
        std::vector<Item> items;
        std::error_code error;
        for (const auto& entry : fs::directory_iterator(m_Project / "captures/changes", error))
        {
            const fs::path file = entry.path() / "change.json";
            std::ifstream stream(file);
            if (!stream) continue;
            const YAML::Node row = YAML::Load(stream);
            items.push_back({file, row["intent"].as<std::string>()});
        }
        std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.File < b.File; });
        m_Items = std::move(items);
        if (m_ID.empty() && !m_Items.empty()) m_ID = m_Items.front().File.parent_path().filename().u8string();

        m_Record.reset();
        for (const Item& item : m_Items)
            if (item.File.parent_path().filename().u8string() == m_ID)
            {
                std::ifstream stream(item.File);
                m_Record.reset(YAML::Load(stream));
                break;
            }
    }

    void AssetChangeReview::Draw()
    {
        try
        {
            if (m_Task.valid() && m_Task.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            {
                const YAML::Node result = m_Task.get();
                m_Message =
                    result["error"] ? result["error"].as<std::string>() : "文件已保存。重新打开该资产后可以看到修改。";
                m_NextPoll = 0;
            }
            if (ImGui::GetTime() >= m_NextPoll)
            {
                Refresh();
                m_NextPoll = ImGui::GetTime() + 1;
            }
            if (m_Items.empty())
            {
                ImGui::TextWrapped("还没有待审的资产修改。用 propose_asset_change 选好资产和字段后，可在这里检查并保存。");
                return;
            }
            ImGui::BeginDisabled(m_Task.valid());
            if (ImGui::BeginCombo("修改记录", m_Record.IsMap() ? m_Record["intent"].Scalar().c_str() : "选择修改"))
            {
                for (const Item& item : m_Items)
                {
                    const std::string id = item.File.parent_path().filename().u8string();
                    if (ImGui::Selectable((item.Intent + "##" + id).c_str(), id == m_ID)) Select(id);
                }
                ImGui::EndCombo();
            }
            ImGui::EndDisabled();
            if (!m_Record.IsMap()) return;

            const bool applied = m_Record["state"].as<std::string>() == "applied";
            const char* state = applied ? "已应用" : m_Record["state"].as<std::string>() == "undone" ? "已撤销" : "待应用";
            ImGui::Text("状态  %s", state);
            int fieldIndex = 0;
            for (const auto& edit : m_Record["operations"])
            {
                ImGui::Separator();
                ImGui::TextWrapped("%s", edit["asset"].as<std::string>().c_str());
                ImGui::TextWrapped("%s", FieldName(edit["field"]).c_str());
                ImGui::PushID(fieldIndex++);
                if (ImGui::BeginTable("修改值", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV))
                {
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("原值");
                    ImGui::TextWrapped("%s", YAML::Dump(edit["before"]).c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("新值");
                    ImGui::TextWrapped("%s", YAML::Dump(edit["after"]).c_str());
                    ImGui::EndTable();
                }
                ImGui::PopID();
            }
            ImGui::Spacing();
            ImGui::BeginDisabled(m_Task.valid());
            if (ImGui::Button(applied ? "撤销这次修改" : "应用到项目文件"))
            {
                YAML::Node request;
                request["action"] = "asset-change";
                request["project"] = m_Project.u8string();
                request["change"] = m_ID;
                request["undo"] = applied;
                m_Task = std::async(std::launch::async, RunForgeTool, "forge_review.py", request);
            }
            ImGui::EndDisabled();
            if (!m_Message.empty()) ImGui::TextWrapped("%s", m_Message.c_str());
        }
        catch (const std::exception& error)
        {
            ImGui::TextWrapped("%s", error.what());
        }
    }
} // namespace Redflag
