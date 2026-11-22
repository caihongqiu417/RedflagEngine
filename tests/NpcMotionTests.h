#pragma once
#include "Redflag/AI/NpcDefinition.h"
#include "Redflag/Animation/ModelAnimation.h"

inline void CheckNpcMotion()
{
    using namespace Redflag;
    NpcDefinition character;
    std::string error;
    Check(character.Load("assets/ai/watt.npc.yaml", error), "load NPC and animator");
    const Ref<ModelAnimation> rig = ModelAnimation::Load("assets/game/station/crew/Operator.glb");
    Check(static_cast<bool>(rig), "load imported operator mesh and animation");
    if (!rig) return;
    for (const AnimationState& state : character.Controller.GetStates())
    {
        const auto& clips = rig->GetClips();
        const auto clip = std::find_if(clips.begin(), clips.end(), [&](const auto& c) { return c.Name == state.Clip; });
        Check(clip != clips.end(), state.Clip.c_str());
        if (clip == clips.end() || (state.ID != "pickup" && state.ID != "give")) continue;
        Check(std::abs(clip->Duration - state.Seconds) < .001f, "action and imported clip use the same seconds");

        std::vector<ModelAnimation::NodePose> local;
        std::vector<glm::mat4> world;
        std::vector<glm::vec3> path;
        float lowestFinger = 10.f;
        const int frames = int(std::round(clip->Duration * 30));
        for (int frame = 0; frame <= frames; ++frame)
        {
            rig->SamplePose(clip - clips.begin(), frame / 30.f, local);
            rig->BuildParts(local, world);
            glm::mat4 hand, finger;
            if (!rig->GetNodeTransform("hand.L", world, hand) || !rig->GetNodeTransform("f_index.03.L", world, finger))
            {
                Check(false, "left wrist and index fingertip exist");
                return;
            }
            path.emplace_back(hand[3]);
            lowestFinger = std::min(lowestFinger, finger[3].y);
        }
        const int contact = int(std::round(frames * state.Contact));
        const auto nearest = state.ID == "pickup"
            ? std::min_element(path.begin(), path.end(), [](glm::vec3 a, glm::vec3 b) { return a.y < b.y; })
            : std::max_element(path.begin(), path.end(), [](glm::vec3 a, glm::vec3 b) { return a.z < b.z; });
        Check(std::abs(int(nearest - path.begin()) - contact) <= 1, "inventory contact agrees with rendered hand");
        Check(glm::distance(path[contact], path.front()) > .45f && glm::distance(path.front(), path.back()) < .04f,
            "wrist reaches away from idle and withdraws");
        Check(lowestFinger > 0, "index fingertip stays above floor");
        std::cout << state.ID << " contact = " << contact << "/" << frames << " frames, wrist XYZ (m) = "
                  << path[contact].x << ", " << path[contact].y << ", " << path[contact].z
                  << ", travel = " << glm::distance(path[contact], path.front()) << " m, lowest index = "
                  << lowestFinger << " m\n";
    }
}
