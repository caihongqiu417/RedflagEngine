#pragma once

#include <array>
#include <chrono>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace Redflag
{
    /* 游戏脚本读取按键，ImGui 读取同一组鼠标动作并判断点击属于游戏还是工具面板。 */
    class PlaytestInput
    {
      public:
        std::string Apply(const std::string &op, const std::string &key,
                          std::optional<std::pair<float, float>> position, int leaseMs);
        std::vector<std::string> BeginFrame();
        void Clear();
        void SendToGui(bool controlling);

        bool Held(int key) const { return m_Keys.count(key) != 0; }
        bool SentMouse(int button) const;
        const auto &Position() const { return m_Position; }

      private:
        std::set<int> m_Keys, m_Taps, m_SentKeys;
        std::map<int, std::pair<std::string, std::chrono::steady_clock::time_point>> m_Leases;
        std::array<bool, 3> m_SentMouse{};
        std::optional<std::pair<float, float>> m_Position;
        bool m_Move = false;
    };
} // namespace Redflag
