#include "Redflag/AI/NpcDefinition.h"
#include "Redflag/Animation/Montage.h"
#include <yaml-cpp/yaml.h>
#include <iostream>

using namespace Redflag;

// ---- 角色信息与候选路径 ----

static YAML::Node Inspect(const NpcDefinition& npc)
{
    YAML::Node result;
    result["name"] = npc.Name;
    result["leader_relationship"] = npc.LeaderRelationship;
    result["voice"] = npc.Voice;
    result["model"] = npc.Model;
    result["remember"] = npc.Remember;
    result["observe_world"] = npc.ObserveWorld;
    YAML::Node watch(YAML::NodeType::Sequence);
    for (const NpcCue& cue : npc.Watch)
    {
        YAML::Node row;
        row["team"] = cue.Team;
        row["clip"] = cue.Clip;
        row["message"] = cue.Message;
        watch.push_back(row);
    }
    result["watch"] = watch;
    result["idle"] = npc.IdleClip;
    result["walk"] = npc.WalkClip;
    result["sight"] = npc.Sight;
    result["search_step"] = npc.SearchStep;
    result["search_radius"] = npc.SearchRadius;
    result["tree"] = npc.TreeFile.filename().u8string();
    result["root"] = npc.Tree.GetRoot();
    result["animator"] = npc.AnimatorFile.filename().u8string();
    YAML::Node states(YAML::NodeType::Sequence);
    for (const AnimationState& state : npc.Controller.GetStates())
    {
        YAML::Node row;
        row["ID"] = state.ID;
        row["Clip"] = state.Clip;
        if (state.MontageAsset) {
            row["Montage"] = state.Montage;
            row["Slot"] = state.MontageAsset->Slot;
        }
        row["Loop"] = state.Loop;
        row["Blend"] = state.Blend;
        row["Contact"] = state.Contact;
        row["Seconds"] = state.Seconds;
        states.push_back(row);
    }
    result["states"] = states;
    YAML::Node rows(YAML::NodeType::Sequence);
    for (const NpcIntent& intent : npc.Intents)
    {
        BehaviorBlackboard actor;
        const BehaviorTrace trace = npc.Tree.Tick(actor, intent.Key);
        YAML::Node row;
        row["candidate"] = intent.Key;
        row["description"] = intent.Description;
        row["target"] = intent.Target;
        row["item"] = intent.Item;
        row["action"] = trace.Action;
        row["nodes"] = trace.Nodes;
        rows.push_back(row);
    }
    result["routes"] = rows;
    return result;
}

int wmain(int argc, wchar_t** argv)
{
    try
    {
        if (argc != 3) throw std::runtime_error("Usage: NpcTool inspect|validate-animator|validate-montage FILE");
        std::string error;
        if (std::wstring(argv[1]) == L"validate-montage") {
            AnimationMontage montage;
            if (!montage.Load(argv[2],error)) throw std::runtime_error(error);
            return 0;
        }
        if (std::wstring(argv[1]) == L"validate-animator")
        {
            AnimationController controller;
            if (!controller.Load(argv[2], error)) throw std::runtime_error(error);
            return 0;
        }
        if (std::wstring(argv[1]) != L"inspect") throw std::runtime_error("Unknown operation");
        NpcDefinition npc;
        if (!npc.Load(argv[2], error))
            throw std::runtime_error(error);

        std::cout << YAML::Dump(Inspect(npc)) << '\n';
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
