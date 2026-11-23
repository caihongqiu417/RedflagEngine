#pragma once

#include "BehaviorTree.h"

namespace Redflag
{
    /* 每个角色单独保存这次选中的分支，Forge 可以在树上标出经过的节点。 */
    struct ActorBehavior
    {
        BehaviorTree Tree;
        BehaviorBlackboard Facts;
        BehaviorTrace Trace;
        std::string File, Error;

        bool Open(const std::string& path)
        {
            if (File != path) {
                File = path;
                Tree.Load(std::filesystem::u8path(path), Error);
            }
            return !File.empty() && Error.empty();
        }
    };
}
