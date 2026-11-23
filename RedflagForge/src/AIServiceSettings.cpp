#include "rfpch.h"
#include "DecisionPanel.h"
#include "Redflag/AI/LocalService.h"
#include "Redflag/Utils/FileSystem.h"
#include <imgui.h>
#include <yaml-cpp/yaml.h>
#include <cstdlib>

namespace Redflag
{
    static std::filesystem::path SettingsFile()
    {
        const wchar_t* folder = _wgetenv(L"LOCALAPPDATA");
        if (!folder) throw std::runtime_error("无法找到当前用户的应用数据目录。");
        return std::filesystem::path(folder) / "Redflag" / "services.json";
    }

    void DecisionPanel::DrawSettingsMenu()
    {
        if (!ImGui::MenuItem("模型与语音设置…")) return;
        m_SettingsOpen = true;
        m_ServiceMessage.clear();
        try
        {
            const auto file = SettingsFile();
            YAML::Node config(YAML::NodeType::Map);
            if (std::filesystem::exists(file)) config = YAML::LoadFile(file.u8string());
            m_External = config["UseExternalModel"].as<bool>(false);
            m_Vision = config["Vision"].as<bool>(false);
            std::snprintf(m_Endpoint, sizeof(m_Endpoint), "%s", config["ModelEndpoint"].as<std::string>("").c_str());
            std::snprintf(m_ServiceModel, sizeof(m_ServiceModel), "%s", config["ModelName"].as<std::string>("").c_str());
            const auto oldVoice = file.parent_path() / "voice.json";
            if (!config["QwenSpeechProject"] && std::filesystem::exists(oldVoice))
                config["QwenSpeechProject"] = YAML::LoadFile(oldVoice.u8string())["QwenSpeechProject"];
            std::snprintf(m_VoiceProject, sizeof(m_VoiceProject), "%s", config["QwenSpeechProject"].as<std::string>("").c_str());
        }
        catch (const std::exception& e) { m_ServiceMessage = e.what(); }
    }

    void DecisionPanel::DrawSettingsWindow()
    {
        if (!m_SettingsOpen) return;
        ImGui::SetNextWindowSize({570, 370}, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("模型与语音设置", &m_SettingsOpen))
        {
            ImGui::TextUnformatted("对话模型");
            if (ImGui::RadioButton("随运行包安装的本地模型", !m_External)) m_External = false;
            if (ImGui::RadioButton("连接自己的模型服务", m_External)) m_External = true;
            if (m_External)
            {
                ImGui::InputTextWithHint("聊天地址", "http://localhost:8000/v1/chat/completions", m_Endpoint, sizeof(m_Endpoint));
                ImGui::InputText("模型名", m_ServiceModel, sizeof(m_ServiceModel));
                ImGui::Checkbox("该模型可读图片", &m_Vision);
            }
            ImGui::Separator();
            ImGui::TextUnformatted("角色声音");
            ImGui::InputText("Delivery 语音项目", m_VoiceProject, sizeof(m_VoiceProject));
            ImGui::TextWrapped("指定装有语音模型的项目目录后，角色可以用资产选定的音色朗读新台词。");
            ImGui::Text("本次连接  模型 %u  ·  语音 %u", unsigned(LocalService::DecisionPort()), unsigned(LocalService::SpeechPort()));
            if (ImGui::Button("保存本机设置"))
            {
                try
                {
                    if (m_External && (!m_Endpoint[0] || !m_ServiceModel[0]))
                        throw std::runtime_error("请填写聊天地址和模型名。");
                    if (m_VoiceProject[0] && !std::filesystem::is_regular_file(std::filesystem::u8path(m_VoiceProject) / "Tools/DeliveryAI/neural_tts.py"))
                        throw std::runtime_error("此目录中没有找到 Delivery 语音工具。");
                    YAML::Emitter out;
                    out.SetStringFormat(YAML::DoubleQuoted);
                    out << YAML::Flow << YAML::BeginMap << YAML::Key << "UseExternalModel" << YAML::Value << m_External
                        << YAML::Key << "ModelEndpoint" << YAML::Value << m_Endpoint << YAML::Key << "ModelName" << YAML::Value << m_ServiceModel
                        << YAML::Key << "Vision" << YAML::Value << m_Vision << YAML::Key << "QwenSpeechProject" << YAML::Value << m_VoiceProject << YAML::EndMap;
                    const auto path = SettingsFile();
                    std::filesystem::create_directories(path.parent_path());
                    FileSystem::WriteText(path, out.c_str());
                    m_ServiceMessage = "已保存。下次从启动脚本打开 Forge 或游戏时生效。";
                }
                catch (const std::exception& e) { m_ServiceMessage = e.what(); }
            }
            if (!m_ServiceMessage.empty()) ImGui::TextWrapped("%s", m_ServiceMessage.c_str());
        }
        ImGui::End();
    }
}
