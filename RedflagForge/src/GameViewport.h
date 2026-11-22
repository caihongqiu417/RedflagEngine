#pragma once

#include <glm/glm.hpp>
#include <imgui.h>
#include <algorithm>

namespace Redflag
{
    /* 游戏按选定的像素数绘制，面板只决定这张图显示多大。场景编辑仍占满面板。 */
    class GameViewport
    {
    public:
        explicit GameViewport(unsigned renderHeight = 1080) : m_Preset(renderHeight == 720 ? 1 : 0) {}

        struct Layout
        {
            glm::uvec2 Pixels;
            ImVec2 Size, Offset;
        };

        void DrawResolution()
        {
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.f);
            ImGui::Combo("游戏分辨率", &m_Preset, "1920 × 1080\0" "1280 × 720\0" "跟随面板\0");
        }

        glm::uvec2 Resolution(glm::uvec2 panel) const
        {
            if (m_Preset == 2) return panel;
            return m_Preset == 0 ? glm::uvec2(1920,1080) : glm::uvec2(1280,720);
        }

        Layout Fit(ImVec2 available, ImVec2 dpi, bool game) const
        {
            glm::uvec2 pixels{std::max(1.f, available.x * dpi.x), std::max(1.f, available.y * dpi.y)};
            if (!game || m_Preset == 2) return {pixels, available, {0,0}};
            pixels = Resolution(pixels);
            const float scale = std::min(available.x * dpi.x / pixels.x, available.y * dpi.y / pixels.y);
            ImVec2 size{pixels.x * scale / dpi.x, pixels.y * scale / dpi.y};
            return {pixels, size, {(available.x-size.x)*.5f, (available.y-size.y)*.5f}};
        }

    private:
        int m_Preset = 0;
    };
}
