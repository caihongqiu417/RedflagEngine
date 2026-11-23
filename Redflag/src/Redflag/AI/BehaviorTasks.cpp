#include "rfpch.h"
#include "BehaviorTree.h"

namespace Redflag
{
    bool BehaviorTree::UsesTasks() const
    {
        const BehaviorNode* root = Find(m_Root);
        return root && (root->Kind == BehaviorKind::Selector || root->Kind == BehaviorKind::Sequence ||
                        root->Kind == BehaviorKind::Decorator || root->Kind == BehaviorKind::Action);
    }

    const BehaviorEdge* BehaviorTree::Child(uint32_t parent, size_t index) const
    {
        for (const BehaviorEdge& edge : m_Edges)
            if (edge.From == parent && index-- == 0) return &edge;
        return nullptr;
    }

    static bool Passes(const BehaviorNode& node, const BehaviorBlackboard& board)
    {
        const auto entry = board.Values.find(node.Key);
        const bool matches = entry != board.Values.end() && (node.Value.empty() || entry->second == node.Value);
        return matches != node.Invert;
    }

    void BehaviorTree::Halt(BehaviorRun& run, const AbortTask& abort) const
    {
        if (!run.Stack.empty())
        {
            run.Aborted = run.Stack.back().Node;
            if (const BehaviorNode* task = Find(run.Aborted)) abort(*task);
        }
        run.Stack.clear();
        run.Conditions.clear();
        run.Trace = {};
    }

    //========== 观察黑板，交还正在执行的动作 ==========

    bool BehaviorTree::Interrupt(const BehaviorBlackboard& board, BehaviorRun& run, const AbortTask& abort) const
    {
        for (size_t depth = 0; depth < run.Stack.size(); ++depth)
        {
            BehaviorRun::Frame& frame = run.Stack[depth];
            const BehaviorNode* node = Find(frame.Node);
            if (!node) continue;
            if (node->Kind == BehaviorKind::Decorator && (node->Abort == BehaviorAbort::Self || node->Abort == BehaviorAbort::Both)
                && !Passes(*node, board))
            {
                run.Aborted = run.Stack.back().Node;
                if (const BehaviorNode* task = Find(run.Aborted)) abort(*task);
                run.Conditions[node->ID] = false;
                run.Trace = {};
                run.Stack.resize(depth);
                return true; // 已离开整个条件分支，向父节点交回 Failure。
            }
            if (node->Kind != BehaviorKind::Selector) continue;

            // Next 已越过当前子节点。只看排在它之前、且先前试过的条件分支。
            for (size_t index = 0; index + 1 < frame.Next; ++index)
            {
                const BehaviorEdge* edge = Child(node->ID, index);
                const BehaviorNode* guard = edge ? Find(edge->To) : nullptr;
                if (!guard || guard->Kind != BehaviorKind::Decorator ||
                    (guard->Abort != BehaviorAbort::LowerPriority && guard->Abort != BehaviorAbort::Both)) continue;
                const bool eligible = Passes(*guard, board);
                const bool changed = eligible && !run.Conditions[guard->ID];
                run.Conditions[guard->ID] = eligible;
                if (!changed) continue;

                run.Aborted = run.Stack.back().Node;
                if (const BehaviorNode* task = Find(run.Aborted)) abort(*task);
                frame.Next = index;
                run.Trace = {};
                run.Stack.resize(depth + 1);
                return false;
            }
        }
        return false;
    }

    //========== 逐帧执行任务 ==========

    BehaviorStatus BehaviorTree::Update(BehaviorBlackboard& board, BehaviorRun& run, const Task& tick, const AbortTask& abort) const
    {
        bool returning = false;
        BehaviorStatus status = BehaviorStatus::Failure;
        if (run.Stack.empty())
        {
            run.Conditions.clear();
            run.Trace = {};
            run.Stack.push_back({m_Root});
        }
        else returning = Interrupt(board, run, abort);

        while (!run.Stack.empty())
        {
            BehaviorRun::Frame& frame = run.Stack.back();
            const BehaviorNode* node = Find(frame.Node);
            if (!node) { run.Stack.pop_back(); returning = true; status = BehaviorStatus::Failure; continue; }

            // Sequence 中途失败、Selector 找到成功的分支时，直接把结果交给上一层。
            if (returning && ((node->Kind != BehaviorKind::Sequence && node->Kind != BehaviorKind::Selector) ||
                (node->Kind == BehaviorKind::Sequence && status == BehaviorStatus::Failure) ||
                (node->Kind == BehaviorKind::Selector && status == BehaviorStatus::Success)))
            {
                run.Stack.pop_back();
                continue;
            }
            returning = false;
            if (node->Kind == BehaviorKind::Action)
            {
                run.Trace = {};
                for (size_t i = 0; i < run.Stack.size(); ++i)
                {
                    const BehaviorRun::Frame& step = run.Stack[i];
                    run.Trace.Nodes.push_back(step.Node);
                    if (i + 1 < run.Stack.size())
                        if (const BehaviorEdge* edge = Child(step.Node, step.Next - 1)) run.Trace.Edges.push_back(*edge);
                }
                board.LastAction = run.Trace.Action = node->Action;
                status = tick(*node);
                if (status == BehaviorStatus::Running) return status;
            }
            else
            {
                const BehaviorEdge* next = nullptr;
                if (node->Kind == BehaviorKind::Sequence || node->Kind == BehaviorKind::Selector)
                    next = Child(node->ID, frame.Next);
                else if (node->Kind == BehaviorKind::Decorator)
                {
                    const bool eligible = Passes(*node, board);
                    run.Conditions[node->ID] = eligible;
                    if (eligible) next = Child(node->ID, 0);
                }
                else
                {
                    next = Branch(*node, board.Candidate, &board);
                    for (frame.Next = 0; next && Child(node->ID, frame.Next) != next; ++frame.Next) {}
                }
                if (next)
                {
                    ++frame.Next;
                    run.Stack.push_back({next->To});
                    continue;
                }
                status = node->Kind == BehaviorKind::Sequence ? BehaviorStatus::Success : BehaviorStatus::Failure;
            }
            run.Stack.pop_back();
            returning = true;
        }
        return status;
    }
}
