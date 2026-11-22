#pragma once

#include <glm/vec2.hpp>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace Redflag
{
    enum class BehaviorKind
    {
        ModelSelector,
        Subtree,
        Action
    };

    struct BehaviorNode
    {
        uint32_t ID;
        BehaviorKind Kind;
        std::string Name, Action;
        glm::vec2 Position; // 编辑器画布坐标。
        std::string Question; // 进入这个选择节点时，带上眼前仍可执行的候选再问一次。
    };

    struct BehaviorEdge
    {
        uint32_t From, To;
        std::string Key; // 选择节点按候选键出发，* 接其余候选。子树入口的线不需要条件。
    };

    struct BehaviorTrace
    {
        std::string Action;
        std::vector<uint32_t> Nodes;
        std::vector<BehaviorEdge> Edges;
        uint32_t Waiting = 0;
    };

    /* 每个角色持有自己的黑板。游戏脚本可以在 Values 里记录任务或交互对象。 */
    struct BehaviorBlackboard
    {
        std::string Candidate, LastAction;
        std::map<std::string, std::string> Values;
    };

    class BehaviorTree
    {
      public:
        bool Load(const std::filesystem::path& path, std::string& error);
        bool Save(const std::filesystem::path& path, std::string& error) const;
        bool Validate(std::string& error) const;

        // 找到动作才更新角色的 LastAction。轨迹包含实际走过的连接，可直接在画布中显示。
        BehaviorTrace Tick(BehaviorBlackboard& actor, const std::string& candidate) const;

        void Advance(BehaviorBlackboard& actor, const std::string& candidate, BehaviorTrace& trace) const;
        bool Offers(uint32_t selector, const std::string& candidate) const;

        uint32_t AddNode(BehaviorKind kind, const std::string& name, glm::vec2 position);
        void RemoveNode(uint32_t id);
        bool Connect(uint32_t from, std::string key, uint32_t to, std::string& error);
        void Disconnect(uint32_t from, std::string key);
        bool SetRoot(uint32_t id);
        uint32_t GetRoot() const { return m_Root; }

        const BehaviorNode* Find(uint32_t id) const;
        void Move(uint32_t id, glm::vec2 position);
        void Rename(uint32_t id, const std::string& name);
        void SetQuestion(uint32_t id, const std::string& question);
        void SetAction(uint32_t id, const std::string& action);
        const std::vector<BehaviorNode>& GetNodes() const { return m_Nodes; }
        const std::vector<BehaviorEdge>& GetEdges() const { return m_Edges; }

      private:
        BehaviorNode* Edit(uint32_t id);
        const BehaviorEdge* Branch(const BehaviorNode& node, const std::string& candidate) const;
        bool Reaches(uint32_t from, uint32_t to) const;
        uint32_t m_Root = 0, m_NextID = 1;
        std::vector<BehaviorNode> m_Nodes;
        std::vector<BehaviorEdge> m_Edges;
    };
} // namespace Redflag
