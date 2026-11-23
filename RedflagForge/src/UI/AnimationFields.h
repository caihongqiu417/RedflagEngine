#pragma once

#include "Redflag/Animation/AnimationController.h"
#include <imgui.h>

namespace Redflag::AnimationFields
{
    inline bool Edit(const char* label, std::string& value, bool multiline = false)
    {
        auto resize = [](ImGuiInputTextCallbackData* event) {
            std::string& text = *static_cast<std::string*>(event->UserData);
            text.resize(event->BufTextLen);
            event->Buf = text.data();
            return 0;
        };
        if (multiline)
            return ImGui::InputTextMultiline(label, value.data(), value.capacity() + 1,
                {-1, ImGui::GetTextLineHeight() * 4}, ImGuiInputTextFlags_CallbackResize, resize, &value);
        return ImGui::InputText(label, value.data(), value.capacity() + 1, ImGuiInputTextFlags_CallbackResize, resize, &value);
    }

    inline bool EditMarker(AnimationMarker& marker)
    {
        bool changed = Edit("事件名称", marker.Name);
        changed |= ImGui::InputFloat(marker.End ? "开始（秒）" : "时刻（秒）", &marker.Start, 0, 0, "%.4f");
        if (marker.End)
            changed |= ImGui::InputFloat("结束（秒）", &*marker.End, 0, 0, "%.4f");
        return changed;
    }
}
