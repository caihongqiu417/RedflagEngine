#pragma once
#include "Redflag/Animation/AnimationTimeline.h"
#include "Redflag/Animation/ModelAnimation.h"
#include "Redflag/Animation/MontagePlayer.h"

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
        {"Station-UseMedkit","f_middle.01.L",0,.6f}, {"Station-Repair","hips",0,.35f},
        {"Station-Revive","f_middle.01.L",0,.65f}, {"Station-Down","head.001",0,1.f},
        {"Station-GetUp","head.001",0,1.f},
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

static void CheckStationCare(const Redflag::ModelAnimation& rig)
{
    using namespace Redflag;
    const auto& clips = rig.GetClips();
    std::vector<ModelAnimation::NodePose> local;
    std::vector<glm::mat4> world;
    auto sample = [&](const char* name, float seconds) {
        const auto clip = std::find_if(clips.begin(), clips.end(), [&](const auto& entry) { return entry.Name == name; });
        if (clip == clips.end()) throw std::runtime_error(name);
        rig.SamplePose(clip-clips.begin(),seconds,local);
        rig.PinMovementRoot(local,"root");
        rig.GetModelTransforms(local,world);
    };
    auto bone = [&](const char* name) {
        glm::mat4 value;
        if (!rig.GetNodeTransform(name,world,value)) throw std::runtime_error(name);
        return value;
    };

    // f_middle.01.L 是药包中心。药包贴上去时，它的后表面应抵住胸甲的第 12275 号顶点。
    const glm::vec4 panelLocal(0,.111770f,.236446f,1);
    const float halfDepth = .04035f;
    float pressError = 0.f, reverseTravel = 0.f, previousY = 0.f, highest = 0.f;
    glm::vec3 start, grip;
    for (int frame=0; frame<=240; ++frame) {
        const float t = frame/120.f;
        sample("Station-UseMedkit",t);
        grip = glm::vec3(bone("f_middle.01.L")[3]);
        const glm::mat4 chest = bone("chest");
        const glm::vec3 surface(chest*panelLocal), front = glm::normalize(glm::vec3(chest[2]));
        if (frame==0) start=grip;
        if (t>=.65f && t<=1.35f) pressError=std::max(pressError,glm::distance(grip-halfDepth*front,surface));
        if (frame>0 && t<=.55f) reverseTravel=std::max(reverseTravel,previousY-grip.y);
        if (t>=1.5f) reverseTravel=std::max(reverseTravel,grip.y-previousY);
        previousY=grip.y;
        highest=std::max(highest,grip.y);
        if (frame==120) {
            const auto parts=rig.BuildParts(local,world);
            float nearest=10.f;
            for (const auto& part : parts) for (const MeshVertex& vertex : part.Vertices)
                nearest=std::min(nearest,glm::distance(vertex.Position,surface));
            Check(nearest<.001f,"chest contact is on the skinned mesh at the healing event");
        }
    }
    Check(pressError<.002f,"medkit back face stays on chest armor before and after the event");
    Check(highest-start.y>.5f && reverseTravel<.002f && glm::distance(start,grip)<.002f,"medkit lifts, stays pressed, then lowers into the starting hand pose");
    std::cout<<"medkit armor_gap_max_m="<<pressError<<" grip_height_m="<<start.y<<","<<highest<<","<<grip.y<<'\n';

    // 对方等待救援时停在倒地末帧。起身首帧与它在全部关节上衔接。
    sample("Station-Down",1.f);
    const std::vector<glm::mat4> down=world;
    const float downHead=bone("head.001")[3].y;
    sample("Station-GetUp",0);
    float jump=0;
    for (size_t i=0;i<world.size();++i) for (int col=0;col<4;++col)
        jump=std::max(jump,glm::length(down[i][col]-world[i][col]));
    sample("Station-GetUp",46.f/30.f);
    const float raisedHead=bone("head.001")[3].y;
    Check(downHead<.35f && raisedHead>1.4f && jump<.001f,"collapsed character begins rising from the same whole-body pose");
    std::cout<<"rescue head_ground_then_raised_m="<<downHead<<","<<raisedHead<<" max_pose_difference="<<jump<<'\n';
}

static void CheckStationActionEvents(const Redflag::Ref<Redflag::ModelAnimation>& rig, const Redflag::AnimationController& controller)
{
    using namespace Redflag;
    AnimationTimeline timeline;
    auto play = [&](const char* id) {
        const AnimationState* state = controller.Find(id);
        if (!state) throw std::runtime_error(std::string("Missing animator state: ") + id);
        for (const ModelAnimation::Clip& clip : rig->GetClips())
            if (clip.Name == state->Clip && timeline.Play(*state, clip.Duration)) return clip.Duration;
        throw std::runtime_error(std::string("Cannot play: ") + id);
    };
    for (const char* id : {"pickup", "give", "heal", "punch", "land", "fire", "repair", "revive", "down", "getup"})
    {
        const AnimationState& state = *controller.Find(id);
        std::vector<std::string> notices;
        if (state.MontageAsset) {
            MontagePlayer player;
            std::string error;
            Check(player.Play(state.MontageAsset,rig,"",id,true,error),id);
            player.Advance(state.Seconds+.01f);
            Check(!player.IsPlaying(state.MontageAsset->Slot),id);
            MontageEvent event;
            uint64_t cursor = 0;
            while (player.Read(cursor,event)) {
                notices.push_back(event.Notice.Name);
                std::cout << "state_event " << id << " " << event.Notice.Name << " at_s=" << event.Notice.Seconds << '\n';
            }
        } else {
            const float duration = play(id);
            timeline.Advance(duration+.01f);
            Check(timeline.Playing() == state.Loop, id);
        }
        if (state.ID=="pickup" || state.ID=="give" || state.ID=="heal")
            Check(notices == std::vector<std::string>{"contact"}, "one inventory contact per reach");
        if (state.ID=="punch")
            Check(notices == std::vector<std::string>{"Contact"}, "one melee contact per punch");
        if (state.ID=="land")
            Check(notices == std::vector<std::string>{"Recovery","KneeBend","Recovery"}, "landing opens and closes recovery around the knee bend");
        play("idle");
        Check(timeline.State()=="idle" && timeline.Playing() && timeline.Time()==0.f, "next idle state starts with its own clock");
    }
}
