#include "rfpch.h"
#include "CreationReviewPanel.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Platform/Vulkan/VulkanDraw.h"
#include <windows.h>
#include <wincrypt.h>
#include <iomanip>

#pragma comment(lib, "crypt32.lib")

namespace Redflag
{
    namespace fs = std::filesystem;

    static fs::path ValidationDirectory()
    {
        const char* configured = std::getenv("REDFLAG_PROJECT");
        const fs::path project = configured && *configured ? fs::absolute(fs::u8path(configured)) : fs::current_path();
        if (project.filename() == "RedflagForge" && fs::exists(project.parent_path() / "premake5.lua"))
            return project.parent_path() / "output/validation";
        return project / "output/validation";
    }

    static YAML::Node ReadRecord(const fs::path& file)
    {
        std::ifstream input(file);
        if (!input) throw std::runtime_error("无法打开审查记录");
        return YAML::Load(input);
    }

    static std::string Digest(const std::vector<uint8_t>& bytes)
    {
        BYTE hash[32];
        DWORD size = sizeof(hash);
        if (bytes.size() > MAXDWORD ||
            !CryptHashCertificate2(L"SHA256", 0, nullptr, bytes.data(), (DWORD)bytes.size(), hash, &size))
            throw std::runtime_error("无法计算文件的 SHA-256");
        std::ostringstream text;
        for (BYTE value : hash) text << std::hex << std::setw(2) << std::setfill('0') << int(value);
        return text.str();
    }

    // ---- 发现记录与核对原文件 ----

    void CreationReviewPanel::List()
    {
        m_Cases.clear();
        std::error_code error;
        for (const auto& entry : fs::directory_iterator(ValidationDirectory(), error))
        {
            const fs::path file = entry.path() / "creation.review.json";
            if (!fs::is_regular_file(file)) continue;
            std::string title = entry.path().filename().u8string();
            try
            {
                title = ReadRecord(file)["title"].as<std::string>();
            }
            catch (const std::exception&)
            { /* 保留条目，选中后可见具体错误。 */
            }
            m_Cases.emplace_back(file, title);
        }
        std::sort(m_Cases.begin(), m_Cases.end());
        if (std::none_of(m_Cases.begin(), m_Cases.end(), [&](const auto& item) { return item.first == m_Selected; }))
            m_Selected = m_Cases.empty() ? fs::path{} : m_Cases.front().first;
    }

    void CreationReviewPanel::Load(VulkanContext& context, VulkanDraw& device)
    {
        if (!m_Files.empty()) context.WaitIdle();
        m_Files.clear();
        m_Record.reset();
        m_Error.clear();
        m_Zoom = -1;
        m_Receipt = -1;
        if (m_Selected.empty()) return;

        try
        {
            YAML::Node record = ReadRecord(m_Selected);
            if (record["version"].as<int>() != 1 || !record["systems"].IsSequence() || !record["steps"].IsSequence() ||
                !record["evidence"].IsSequence())
                throw std::runtime_error("记录缺少改动、复测步骤或证据，或版本无法读取");
            record["title"].as<std::string>();
            record["intent"].as<std::string>();
            for (const auto& row : record["systems"])
            {
                row["name"].as<std::string>();
                row["change"].as<std::string>();
            }
            for (const auto& row : record["steps"])
            {
                row["action"].as<std::string>();
                row["expect"].as<std::string>();
            }
            const fs::path folder = fs::canonical(m_Selected.parent_path());
            for (const auto& row : record["evidence"])
            {
                Evidence data;
                data.Label = row["label"].as<std::string>();
                data.File = row["file"].as<std::string>();
                try
                {
                    const fs::path local = fs::u8path(data.File);
                    const fs::path file = fs::weakly_canonical(folder / local);
                    const fs::path within = file.lexically_relative(folder);
                    if (local.has_root_path() || within.empty() || *within.begin() == "..")
                        throw std::runtime_error("文件不在本案例目录中");
                    std::string ext = file.extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(),
                                   [](unsigned char c) { return char(std::tolower(c)); });
                    data.Image = ext == ".png" || ext == ".jpg" || ext == ".jpeg";
                    if (!data.Image && ext != ".txt" && ext != ".json" && ext != ".csv" && ext != ".tsv" && ext != ".md")
                        throw std::runtime_error("只支持图片和文字回执");

                    std::ifstream input(file, std::ios::binary);
                    if (!input) throw std::runtime_error("找不到证据文件");
                    const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input), {}};
                    if (Digest(bytes) != row["sha256"].as<std::string>())
                        throw std::runtime_error("文件已变动，请核对并重新发布记录");
                    if (data.Image)
                        data.Picture.Load(device, bytes);
                    else
                        data.Text.assign(bytes.begin(), bytes.end());
                }
                catch (const std::exception& error)
                {
                    data.Error = error.what();
                }
                if (m_Receipt < 0 && !data.Image) m_Receipt = int(m_Files.size());
                m_Files.push_back(std::move(data));
            }
            m_Record.reset(record);
        }
        catch (const std::exception& error)
        {
            m_Error = error.what();
        }
    }

    // ---- 图片与文字回执 ----

    void CreationReviewPanel::DrawImages()
    {
        bool any = false;
        if (ImGui::BeginTable("证据图片", 2, ImGuiTableFlags_SizingStretchSame))
        {
            for (int i = 0; i < (int)m_Files.size(); ++i)
            {
                const Evidence& file = m_Files[i];
                if (!file.Image) continue;
                any = true;
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", file.Label.c_str());
                if (!file.Error.empty())
                {
                    ImGui::TextWrapped("%s", file.Error.c_str());
                    continue;
                }
                const float width = std::min(float(file.Picture.Width), ImGui::GetContentRegionAvail().x);
                file.Picture.Draw(width);
                if (ImGui::IsItemClicked()) m_Zoom = i;
                ImGui::TextDisabled("%d × %d", file.Picture.Width, file.Picture.Height);
                ImGui::PushID(i);
                ImGui::SameLine();
                if (ImGui::SmallButton("放大")) m_Zoom = i;
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (!any) ImGui::TextDisabled("这个案例只附有文字回执。");
        if (m_Zoom >= 0)
        {
            ImGui::OpenPopup("查看原图");
            bool open = true;
            const auto& f = m_Files[m_Zoom];
            const ImVec2 screen = ImGui::GetMainViewport()->WorkSize;
            const float scale = std::min({1.f, (screen.x * .88f) / f.Picture.Width, (screen.y * .80f) / f.Picture.Height});
            if (ImGui::BeginPopupModal("查看原图", &open, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::TextWrapped("%s   %s", f.Label.c_str(), f.File.c_str());
                f.Picture.Draw(f.Picture.Width * scale);
                if (ImGui::Button("返回记录") || ImGui::IsKeyPressed(ImGuiKey_Escape))
                {
                    ImGui::CloseCurrentPopup();
                    open = false;
                }
                ImGui::EndPopup();
            }
            if (!open) m_Zoom = -1;
        }
    }

    void CreationReviewPanel::DrawReceipts()
    {
        if (m_Receipt < 0)
        {
            ImGui::TextDisabled("这个案例只附有图片。");
            return;
        }
        if (ImGui::BeginCombo("回执文件", m_Files[m_Receipt].Label.c_str()))
        {
            for (int i = 0; i < (int)m_Files.size(); ++i)
                if (!m_Files[i].Image && ImGui::Selectable(m_Files[i].Label.c_str(), i == m_Receipt)) m_Receipt = i;
            ImGui::EndCombo();
        }
        const Evidence& file = m_Files[m_Receipt];
        ImGui::TextDisabled("%s", file.File.c_str());
        if (!file.Error.empty())
        {
            ImGui::TextWrapped("%s", file.Error.c_str());
            return;
        }
        if (ImGui::Button("复制回执")) ImGui::SetClipboardText(file.Text.c_str());
        ImGui::Separator();
        ImGui::PushTextWrapPos(0.f);
        ImGui::TextUnformatted(file.Text.data(), file.Text.data() + file.Text.size());
        ImGui::PopTextWrapPos();
    }

    void CreationReviewPanel::DrawMenu()
    {
        if (ImGui::MenuItem("创作审查", "Ctrl+Shift+U", &m_Open)) m_Refresh = m_Open;
    }

    void CreationReviewPanel::Draw(VulkanContext& context, VulkanDraw& device)
    {
        if (m_Startup)
        {
            m_Startup = false;
            const char* requested = std::getenv("REDFLAG_REVIEW");
            const char* page = std::getenv("REDFLAG_REVIEW_PAGE");
            if (requested && *requested)
            {
                m_Open = true;
                m_DeepLink = requested;
                if (page) m_Page = page;
            }
        }
        ImGuiIO& io = ImGui::GetIO();
        if (!io.WantTextInput && io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_U, false))
        {
            m_Open = !m_Open;
            m_Refresh = m_Open;
        }
        if (!m_Open) { m_Playtest.StopAudio(); return; }
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_FirstUseEver, {.5f, .5f});
        ImGui::SetNextWindowSize({std::min(860.f, viewport->WorkSize.x * .9f), std::min(670.f, viewport->WorkSize.y * .88f)},
                                 ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("创作审查", &m_Open))
        {
            ImGui::End();
            return;
        }
        if (ImGui::BeginTabBar("审查方式"))
        {
            if (ImGui::BeginTabItem("创作记录"))
            {
                DrawRecords(context, device);
                ImGui::EndTabItem();
            }
            const bool jump = m_Page == "playtest" || m_Page == "jobs";
            if (ImGui::BeginTabItem("试玩回放", nullptr, jump ? ImGuiTabItemFlags_SetSelected : 0))
            {
                if (jump)
                {
                    m_Playtest.Select(m_DeepLink, m_Page == "jobs");
                    m_Page.clear();
                    m_DeepLink.clear();
                }
                m_Playtest.Draw(context, device);
                ImGui::EndTabItem();
            }
            else m_Playtest.StopAudio();
            const bool change = m_Page == "change";
            if (ImGui::BeginTabItem("资产修改", nullptr, change ? ImGuiTabItemFlags_SetSelected : 0))
            {
                if (change)
                {
                    m_Changes.Select(m_DeepLink);
                    m_Page.clear();
                    m_DeepLink.clear();
                }
                m_Changes.Draw();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::End();
    }

    void CreationReviewPanel::DrawRecords(VulkanContext& context, VulkanDraw& device)
    {
        if (m_Refresh)
        {
            List();
            if (!m_DeepLink.empty() && m_Page != "playtest" && m_Page != "jobs" && m_Page != "change")
            {
                for (const auto& [file, title] : m_Cases)
                    if (file.parent_path().filename().u8string() == m_DeepLink) m_Selected = file;
                m_DeepLink.clear();
            }
            Load(context, device);
            m_Refresh = false;
        }
        const char* title = m_Record.IsMap() ? m_Record["title"].Scalar().c_str() : "选择案例";
        if (ImGui::BeginCombo("案例", title))
        {
            for (const auto& [file, label] : m_Cases)
                if (ImGui::Selectable(label.c_str(), file == m_Selected) && file != m_Selected)
                {
                    m_Selected = file;
                    Load(context, device);
                }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button("重新核对")) m_Refresh = true;
        if (!m_Error.empty())
            ImGui::TextWrapped("%s", m_Error.c_str());
        else if (m_Cases.empty())
            ImGui::TextWrapped("还没有审查记录。请在本项目的 output/validation/案例名 中发布 creation.review.json。");
        else if (ImGui::BeginTabBar("审查内容"))
        {
            if (ImGui::BeginTabItem("改动与复测"))
            {
                ImGui::TextWrapped("%s", m_Record["intent"].Scalar().c_str());
                ImGui::Separator();
                for (const auto& row : m_Record["systems"])
                {
                    ImGui::TextUnformatted(row["name"].Scalar().c_str());
                    ImGui::TextWrapped("%s", row["change"].Scalar().c_str());
                    ImGui::Spacing();
                }
                ImGui::Separator();
                ImGui::TextUnformatted("复测步骤");
                int number = 0;
                for (const auto& step : m_Record["steps"])
                {
                    ImGui::TextWrapped("%d. %s", ++number, step["action"].Scalar().c_str());
                    ImGui::Indent();
                    ImGui::TextWrapped("%s", step["expect"].Scalar().c_str());
                    ImGui::Unindent();
                    ImGui::Spacing();
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("图片", nullptr, m_Page == "images" ? ImGuiTabItemFlags_SetSelected : 0))
            {
                if (m_Page == "images") m_Page.clear();
                DrawImages();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("文字回执", nullptr, m_Page == "receipts" ? ImGuiTabItemFlags_SetSelected : 0))
            {
                if (m_Page == "receipts") m_Page.clear();
                DrawReceipts();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
} // namespace Redflag
