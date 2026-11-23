#include "rfpch.h"
#include "AnimationPoseView.h"
#include "Redflag/Animation/ModelAnimation.h"
#include <imgui.h>

namespace Redflag
{
    struct PoseSample
    {
        float Time = 0;
        std::vector<std::pair<glm::vec3, glm::vec3>> Bones;
        std::string Error;
    };

    static PoseSample Sample(const ModelAnimation& model, const std::string& name, float time)
    {
        PoseSample sample;
        sample.Time = time;
        const auto& clips = model.GetClips();
        const auto clip = std::find_if(clips.begin(), clips.end(), [&](const auto& row) { return row.Name == name; });
        if (clip == clips.end()) sample.Error = "模型没有这个剪辑";
        else if (!(time >= 0 && time <= clip->Duration)) sample.Error = "时间超出剪辑范围";
        else
        {
            sample.Bones = model.SampleBones(clip - clips.begin(), time);
            if (sample.Bones.empty()) sample.Error = "这段模型没有蒙皮骨架";
        }
        return sample;
    }

    static glm::vec2 Project(glm::vec3 point)
    {
        // 侧前方看关节。横向稍微带入模型的前后距离，可以把两条腿分开。
        return { .6f * point.x + .8f * point.z, -point.y };
    }

    void DrawAnimationPose(const ModelAnimation& model, const std::string& savedClip,
                           const std::vector<AnimationMarker>& savedMarkers, const std::string& draftClip,
                           const AnimationMarker& marker, bool atEnd)
    {
        PoseSample poses[2];
        const auto old = std::find_if(savedMarkers.begin(), savedMarkers.end(),
            [&](const AnimationMarker& entry) { return entry.Name == marker.Name; });
        if (old != savedMarkers.end() && (!atEnd || old->End))
            poses[0] = Sample(model, savedClip, atEnd ? *old->End : old->Start);
        else
            poses[0].Error = "已保存的动作没有这个时刻";
        poses[1] = Sample(model, draftClip, atEnd ? *marker.End : marker.Start);

        glm::vec2 low(FLT_MAX), high(-FLT_MAX);
        for (const PoseSample& pose : poses)
            for (const auto& [a,b] : pose.Bones)
            {
                low = glm::min(low, glm::min(Project(a), Project(b)));
                high = glm::max(high, glm::max(Project(a), Project(b)));
            }
        const float width = ImGui::GetContentRegionAvail().x;
        const float half = width * .5f;
        const float height = 185.f;
        const ImVec2 start = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(width, height));
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(start, {start.x + width, start.y + height}, ImGui::GetColorU32(ImGuiCol_ChildBg));
        draw->AddLine({start.x+half,start.y}, {start.x+half,start.y+height}, ImGui::GetColorU32(ImGuiCol_Separator));

        // 两边共用尺寸和地面位置，人物屈膝时不会被各自放大到同样高。
        const bool visible = !poses[0].Bones.empty() || !poses[1].Bones.empty();
        const glm::vec2 extent = visible ? glm::max(high - low, glm::vec2(.001f)) : glm::vec2(1.f);
        const float scale = std::min((half - 18.f) / extent.x, (height - 32.f) / extent.y);
        for (int view = 0; view < 2; ++view)
        {
            const float x = start.x + view * half;
            const PoseSample& pose = poses[view];
            char caption[96];
            std::snprintf(caption, sizeof(caption), "%s   %.4f 秒", view ? "当前编辑" : "已保存", pose.Time);
            draw->AddText({x+5,start.y+2}, ImGui::GetColorU32(ImGuiCol_Text), caption);
            if (!pose.Error.empty())
            {
                draw->AddText({x+5,start.y+35}, ImGui::GetColorU32(ImGuiCol_TextDisabled), pose.Error.c_str());
                continue;
            }
            auto screen = [&](glm::vec3 point) {
                const glm::vec2 p = Project(point);
                return ImVec2(x+half*.5f+(p.x-(low.x+high.x)*.5f)*scale,
                              start.y+height-5.f+(p.y-high.y)*scale);
            };
            const ImU32 color = view ? IM_COL32(241,178,104,255) : IM_COL32(117,198,215,255);
            for (const auto& [a,b] : pose.Bones)
            {
                draw->AddLine(screen(a),screen(b),color,2.f);
                draw->AddCircleFilled(screen(b),2.f,color);
            }
        }
    }
}
