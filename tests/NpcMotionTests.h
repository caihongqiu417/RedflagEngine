#pragma once
#include "Redflag/AI/NpcDefinition.h"
#include "Redflag/Animation/ModelAnimation.h"
#include "StationActionClips.h"

inline void CheckOperatorMotion(const Redflag::ModelAnimation& rig)
{
    using namespace Redflag;
    const auto& clips = rig.GetClips();
    auto find = [&](const char* name) {
        auto clip = std::find_if(clips.begin(), clips.end(), [&](const auto& entry) { return entry.Name == name; });
        if (clip == clips.end()) throw std::runtime_error(std::string("Missing operator clip: ") + name);
        return size_t(clip-clips.begin());
    };
    std::vector<ModelAnimation::NodePose> local;
    std::vector<glm::mat4> world;
    auto pose = [&](size_t clip, float seconds, bool inPlace) {
        rig.SamplePose(clip, seconds, local);
        if (inPlace) rig.PinMovementRoot(local, "root");
        rig.GetModelTransforms(local, world);
    };
    auto joint = [&](const char* name) {
        glm::mat4 transform;
        if (!rig.GetNodeTransform(name, world, transform)) throw std::runtime_error(std::string("Missing joint: ") + name);
        return glm::vec3(transform[3]);
    };

    // 游戏按根轨迹的水平距离推进时间，渲染时固定根节点，以免角色跟着骨骼再移动一次。
    for (const char* name : {"walking_nogun-cycle", "running_nogun-cycle", "Station-CrouchWalk"})
    {
        size_t clip = find(name);
        float duration = clips[clip].Duration;
        pose(clip, 0.f, false);
        const glm::vec3 start = joint("root");
        pose(clip, duration, false);
        const glm::vec3 stride = joint("root") - start;
        pose(clip, 0.f, true);
        const float leading = joint("foot.L").z - joint("foot.R").z;
        pose(clip, duration * .5f, true);
        const float middle = joint("foot.L").z - joint("foot.R").z;
        Check(leading > .25f && middle < -.25f && stride.z > 1.f && std::abs(stride.x) < .01f,
              "feet take turns leading while movement root travels forward");
        pose(clip, duration, true);
        Check(glm::distance(start,joint("root")) < .001f, "pinned movement root stays on the actor");
        std::cout << name << " period_s=" << duration << " stride_m=" << stride.z << " speed_m/s=" << stride.z/duration
                  << " left_minus_right_m=" << leading << "," << middle << '\n';
    }
    glm::vec3 standingCenter(0.f);
    const size_t walking = find("walking_nogun-cycle");
    for (int i=0; i<60; ++i) {
        pose(walking, clips[walking].Duration*i/60.f, true);
        standingCenter += joint("hips") / 60.f;
    }
    for (const char* name : {"Idle-cycle", "Idlecombat-cycle", "AIM-Center", "Cannon_Charge", "Cannon_activation-noexp"}) {
        pose(find(name), 0.f, true);
        const glm::vec3 offset = joint("hips") - standingCenter;
        Check(std::hypot(offset.x,offset.z) < .001f, "standing clips begin over the same stance as walking");
    }
    pose(find("Station-Crouch"), 0.f, true);
    const float crouched = joint("hips").y;
    pose(find("Idle-cycle"), 0.f, true);
    Check(joint("hips").y - crouched > .2f, "crouched pelvis is lower than standing");

    const size_t punch = find("Station-Punch");
    pose(punch, 0, true);
    const glm::vec3 first = joint("hand.L"), knuckle = joint("f_middle.01.L");
    const auto guard = rig.BuildParts(local,world);
    float reach = first.z;
    int contact = 0;
    for (int frame = 1; frame <= 26; ++frame)
    {
        pose(punch, frame/30.f, true);
        const float z = joint("hand.L").z;
        if (z > reach) { reach = z; contact = frame; }
    }
    Check(glm::distance(joint("hand.L"),first) < .002f, "jab returns to its starting guard");
    pose(punch, contact/30.f, true);
    const glm::vec3 shoulder = joint("upper_arm.L"), elbow = joint("forearm.L"), fist = joint("hand.L");
    Check(contact == 6 && reach-first.z > .45f && elbow.z > shoulder.z && fist.z > elbow.z,
          "shoulder, elbow and wrist extend forward on the source contact frame");

    // 追踪起手时拳峰附近的同一组网格顶点，检查画出的手确实随骨骼前移。
    const auto extended = rig.BuildParts(local,world);
    float total = 0.f;
    size_t vertices = 0;
    for (size_t part = 0; part < guard.size(); ++part)
        for (size_t i = 0; i < guard[part].Vertices.size(); ++i)
            if (glm::distance(guard[part].Vertices[i].Position, knuckle) < .04f)
            {
                total += extended[part].Vertices[i].Position.z - guard[part].Vertices[i].Position.z;
                ++vertices;
            }
    Check(vertices > 0 && total / vertices > .45f, "skinned knuckle vertices travel with the punch");
    std::cout << "Station-Punch contact_s=" << contact/30.f << " shoulder/elbow/wrist_z_m=" << shoulder.z
              << "," << elbow.z << "," << fist.z << " wrist_travel_z_m=" << reach-first.z
              << " tracked_vertices=" << vertices << " mean_vertex_travel_z_m=" << total/vertices << '\n';

    // 脚踝的高度还包括靴底厚度。直接检查蒙皮后的所有顶点是否留在水平地面附近。
    for (const char* name : {"Station-Punch", "Station-Crouch", "Station-CrouchWalk"})
    {
        const size_t clip = find(name);
        const int frames = int(std::round(clips[clip].Duration * 60));
        float minimum = 100.f, maximum = -100.f;
        for (int frame = 0; frame <= frames; ++frame)
        {
            pose(clip, frame/60.f, true);
            float floor = 100.f;
            for (const auto& part : rig.BuildParts(local,world))
                for (const MeshVertex& vertex : part.Vertices) floor = std::min(floor,vertex.Position.y);
            minimum = std::min(floor,minimum);
            maximum = std::max(floor,maximum);
        }
        Check(minimum > -.01f && maximum < .01f, "lowest skinned vertex stays within 1 cm of flat ground");
        std::cout << name << " sole_range_y_m=" << minimum << "," << maximum << '\n';
    }
}

inline void CheckNpcMotion()
{
    using namespace Redflag;
    NpcDefinition character;
    std::string error;
    Check(character.Load("assets/ai/watt.npc.yaml", error), "load NPC and animator");
    const Ref<ModelAnimation> rig = ModelAnimation::Load("assets/game/station/crew/Operator.glb");
    Check(static_cast<bool>(rig), "load imported operator mesh and animation");
    if (!rig) return;
    CheckOperatorMotion(*rig);
    CheckStationActionClips(*rig, false);
    CheckStationCare(*rig);
    CheckStationActionEvents(rig, character.Controller);
    const auto sentinel = ModelAnimation::Load("assets/game/station/crew/Sentinel.glb");
    Check(bool(sentinel), "load actual station guard");
    if (sentinel) CheckStationActionClips(*sentinel, true);
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
