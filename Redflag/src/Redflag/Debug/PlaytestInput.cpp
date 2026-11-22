#include "rfpch.h"
#include "PlaytestInput.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <map>

namespace Redflag
{
    namespace
    {
        // GLFW 鼠标编号与键盘编号分别从零计数。在同一份按下记录里给鼠标单独留三个位置。
        constexpr int MouseBase = GLFW_KEY_LAST + 1;

        int Code(const std::string &key)
        {
            if (key.size() == 1 && ((key[0] >= 'A' && key[0] <= 'Z') || (key[0] >= '0' && key[0] <= '9')))
                return key[0];
            static const std::map<std::string, int> named = {
                {"Space", GLFW_KEY_SPACE},     {"Shift", GLFW_KEY_LEFT_SHIFT}, {"Ctrl", GLFW_KEY_LEFT_CONTROL},
                {"Enter", GLFW_KEY_ENTER},     {"Escape", GLFW_KEY_ESCAPE},    {"F1", GLFW_KEY_F1},
                {"Up", GLFW_KEY_UP},           {"Down", GLFW_KEY_DOWN},        {"Left", GLFW_KEY_LEFT},
                {"Right", GLFW_KEY_RIGHT},     {"MouseLeft", MouseBase},       {"MouseRight", MouseBase + 1},
                {"MouseMiddle", MouseBase + 2}};
            const auto found = named.find(key);
            return found == named.end() ? -1 : found->second;
        }

        ImGuiKey GuiKey(int key)
        {
            if (key >= 'A' && key <= 'Z') return ImGuiKey(ImGuiKey_A + key - 'A');
            if (key >= '0' && key <= '9') return ImGuiKey(ImGuiKey_0 + key - '0');
            switch (key)
            {
            case GLFW_KEY_SPACE:
                return ImGuiKey_Space;
            case GLFW_KEY_LEFT_SHIFT:
                return ImGuiKey_LeftShift;
            case GLFW_KEY_LEFT_CONTROL:
                return ImGuiKey_LeftCtrl;
            case GLFW_KEY_ENTER:
                return ImGuiKey_Enter;
            case GLFW_KEY_ESCAPE:
                return ImGuiKey_Escape;
            case GLFW_KEY_F1:
                return ImGuiKey_F1;
            case GLFW_KEY_UP:
                return ImGuiKey_UpArrow;
            case GLFW_KEY_DOWN:
                return ImGuiKey_DownArrow;
            case GLFW_KEY_LEFT:
                return ImGuiKey_LeftArrow;
            case GLFW_KEY_RIGHT:
                return ImGuiKey_RightArrow;
            default:
                return ImGuiKey_None;
            }
        }
    } // namespace

    // ---- 脚本输入 ----
    std::string PlaytestInput::Apply(const std::string &op, const std::string &key,
                                     std::optional<std::pair<float, float>> position, int leaseMs)
    {
        if (op == "move" && !position) return "missing_position";
        if (op != "move")
        {
            const int code = Code(key);
            if (code < 0) return "unknown_key";
            if (op == "hold" && (leaseMs < 1 || leaseMs > 30000)) return "invalid_lease_ms";
            if (op == "hold" || op == "tap")
                m_Keys.insert(code);
            else if (op == "release")
                m_Keys.erase(code);
            else
                return "unknown_operation";
            if (op == "tap") m_Taps.insert(code);
            if (op == "hold")
                m_Leases[code] = {key, std::chrono::steady_clock::now() + std::chrono::milliseconds(leaseMs)};
            else
                m_Leases.erase(code);
        }
        if (position)
        {
            m_Position = position;
            m_Move = true;
        }
        return {};
    }

    std::vector<std::string> PlaytestInput::BeginFrame()
    {
        for (int key : m_Taps)
            m_Keys.erase(key);
        m_Taps.clear();
        std::vector<std::string> expired;
        const auto now = std::chrono::steady_clock::now();
        for (auto it = m_Leases.begin(); it != m_Leases.end();)
        {
            if (it->second.second > now)
            {
                ++it;
                continue;
            }
            m_Keys.erase(it->first);
            expired.push_back(it->second.first);
            it = m_Leases.erase(it);
        }
        return expired;
    }

    void PlaytestInput::Clear()
    {
        m_Keys.clear();
        m_Taps.clear();
        m_Leases.clear();
        m_SentMouse.fill(false);
        m_Position.reset();
        m_Move = false;
    }

    bool PlaytestInput::SentMouse(int button) const
    {
        return button >= 0 && button < int(m_SentMouse.size()) && m_SentMouse[button];
    }

    // ---- 交给界面的鼠标与快捷键 ----
    void PlaytestInput::SendToGui(bool controlling)
    {
        ImGuiIO &io = ImGui::GetIO();
        // 有新的操作或仍在拖动时再送坐标，平时让人可以把鼠标移到接管按钮上。
        bool dragging = false;
        for (int i = 0; i < 3; ++i)
            dragging |= Held(MouseBase + i) || m_SentMouse[i];
        if (controlling && m_Position && (m_Move || dragging))
        {
            ImVec2 origin = ImGui::GetMainViewport()->Pos;
            io.AddMousePosEvent(origin.x + m_Position->first, origin.y + m_Position->second);
            m_Move = false;
        }
        auto send = [&](int key, bool held) {
            if (key >= MouseBase)
                io.AddMouseButtonEvent(key - MouseBase, held);
            else
                io.AddKeyEvent(GuiKey(key), held);
        };
        for (int key : m_SentKeys)
            if (!Held(key)) send(key, false);
        for (int key : m_Keys)
            if (!m_SentKeys.count(key)) send(key, true);
        m_SentKeys = m_Keys;
        for (int i = 0; i < 3; ++i)
            m_SentMouse[i] = Held(MouseBase + i);
    }
} // namespace Redflag
