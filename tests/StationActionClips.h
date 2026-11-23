#pragma once
#include "Redflag/Animation/AnimationTimeline.h"
#include "Redflag/Animation/ModelAnimation.h"

// 只检查正式模型和事件时间轴。游戏中何时选这些剪辑由玩家脚本和随从状态机决定。
static void CheckStationActionClips(const Redflag::ModelAnimation& rig, bool sentinel)
{
    using namespace Redflag;
    struct Case { const char* Clip; const char* Joint; float Seconds; float MinimumTravel; };
    // Seconds 为 0 时采完整剪辑。其余值来自游戏实际使用的前段或停住的时刻。
    std::vector<Case> cases = sentinel ? std::vector<Case>{
        {"Idle-cycle","Body",0,.05f}, {"Walk-cycle","L-RearFoot",0,.2f},
        {"Cannon_Fire_Recoil","CannonAnimRecoil",0,.2f}, {"Damage_heavy","Body",.3f,.08f},
    } : std::vector<Case>{
        {"AIM-Center","hand.R",0,.45f}, {"Cannon_activation-noexp","hand.R",.35f,.1f},
        {"Cannon_Charge","hand.R",.22f,.2f}, {"Cannon_Charge-cycle","hand.R",0,.009f},
        {"jump_1_up","foot.L",0,.035f}, {"jump_3_midair-cycle","foot.L",0,.025f},
        {"jump_5_hardlanding","hips",0,.09f}, {"flinch_heavy","head.001",0,.3f},
        {"Station-Pickup","hand.L",0,.5f}, {"Station-Give","hand.L",0,.5f},
    };
    const auto& clips = rig.GetClips();
    auto find = [&](const char* name) {
        auto it = std::find_if(clips.begin(), clips.end(), [&](const auto& clip) { return clip.Name == name; });
        if (it == clips.end()) throw std::runtime_error(std::string("Missing expected clip: ") + name);
        return size_t(it-clips.begin());
    };
    const size_t idle = find("Idle-cycle");
    std::vector<ModelAnimation::NodePose> local;
    std::vector<glm::mat4> world;
    for (const Case& test : cases)
    {
        const size_t clip = find(test.Clip);
        const float duration = clips[clip].Duration, end = test.Seconds > 0 ? test.Seconds : duration;
        auto sample = [&](size_t index, float seconds) {
            rig.SamplePose(index, seconds, local);
            if (!sentinel) rig.PinMovementRoot(local, "root");
            rig.GetModelTransforms(local, world);
            glm::mat4 joint;
            if (!rig.GetNodeTransform(test.Joint, world, joint)) throw std::runtime_error(test.Joint);
            return glm::vec3(joint[3]);
        };
        // 瞄准本来只有一帧，要和放下臂炮时的手腕比较。
        const glm::vec3 first = sample(duration == 0 ? idle : clip, 0);
        const int count = std::max(1, int(std::ceil(end * 30.f)));
        float travel = 0;
        bool finite = true;
        for (int i = 0; i <= count; ++i)
        {
            const glm::vec3 position = sample(clip, end*i/count);
            travel = std::max(travel, glm::distance(first,position));
            for (const glm::mat4& bone : world)
                for (int column = 0; column < 4; ++column)
                    for (int row = 0; row < 4; ++row) finite &= std::isfinite(bone[column][row]);
        }
        Check(finite && end <= duration+.00001f && travel > test.MinimumTravel, test.Clip);
        std::cout << "clip_check " << test.Clip << " duration_s=" << duration << " observed_s=" << end
                  << " joint=" << test.Joint << " travel_m=" << travel << " nodes=" << world.size() << '\n';
    }
}

static void CheckStationActionEvents(const Redflag::ModelAnimation& rig, const Redflag::AnimationController& controller)
{
    using namespace Redflag;
    AnimationTimeline timeline;
    auto play = [&](const char* id) {
        const AnimationState* state = controller.Find(id);
        if (!state) throw std::runtime_error(std::string("Missing animator state: ") + id);
        for (const ModelAnimation::Clip& clip : rig.GetClips())
            if (clip.Name == state->Clip && timeline.Play(*state, clip.Duration)) return clip.Duration;
        throw std::runtime_error(std::string("Cannot play: ") + id);
    };
    for (const char* id : {"pickup", "give", "punch", "land", "repair", "revive"})
    {
        const float duration = play(id);
        const bool looping = controller.Find(id)->Loop;
        timeline.Advance(duration+.01f);
        Check(timeline.Playing() == looping, id);
        AnimationNotice event;
        std::vector<std::string> notices;
        while (timeline.Read(event))
        {
            notices.push_back(event.Name);
            std::cout << "state_event " << id << " " << event.Name << " at_s=" << event.Seconds << '\n';
        }
        const std::string state = id;
        if (state=="pickup" || state=="give")
            Check(notices == std::vector<std::string>{"contact"}, "one inventory contact per reach");
        if (state=="punch")
            Check(notices == std::vector<std::string>{"Contact"}, "one melee contact per punch");
        if (state=="land")
            Check(notices == std::vector<std::string>{"Recovery","KneeBend","Recovery"}, "landing opens and closes recovery around the knee bend");
        play("idle");
        Check(timeline.State()=="idle" && timeline.Playing() && timeline.Time()==0.f, "next idle state starts with its own clock");
    }
}
