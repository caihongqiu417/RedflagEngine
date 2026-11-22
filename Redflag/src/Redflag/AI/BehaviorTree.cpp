#include "rfpch.h"
#include "BehaviorTree.h"
#include "Redflag/Utils/FileSystem.h"
#include <yaml-cpp/yaml.h>
#include <unordered_set>

namespace Redflag
{
    static const char* KindName(BehaviorKind kind)
    {
        switch (kind)
        {
        case BehaviorKind::ModelSelector:
            return "ModelSelector";
        case BehaviorKind::Subtree:
            return "Subtree";
        case BehaviorKind::Action:
            return "Action";
        }
        throw std::runtime_error("未知的行为节点类型。");
    }

    static BehaviorKind ReadKind(const std::string& name)
    {
        for (BehaviorKind kind : {BehaviorKind::ModelSelector, BehaviorKind::Subtree, BehaviorKind::Action})
            if (name == KindName(kind))
                return kind;
        throw std::runtime_error("未知的行为节点类型：" + name);
    }

    // ---- 节点与根节点 ----

    BehaviorNode* BehaviorTree::Edit(uint32_t id)
    {
        for (BehaviorNode& node : m_Nodes)
            if (node.ID == id)
                return &node;
        return nullptr;
    }

    const BehaviorNode* BehaviorTree::Find(uint32_t id) const
    {
        for (const BehaviorNode& node : m_Nodes)
            if (node.ID == id)
                return &node;
        return nullptr;
    }

    void BehaviorTree::Move(uint32_t id, glm::vec2 position)
    {
        if (BehaviorNode* node = Edit(id))
            node->Position = position;
    }

    void BehaviorTree::Rename(uint32_t id, const std::string& name)
    {
        if (BehaviorNode* node = Edit(id))
            node->Name = name;
    }

    void BehaviorTree::SetAction(uint32_t id, const std::string& action)
    {
        if (BehaviorNode* node = Edit(id); node && node->Kind == BehaviorKind::Action)
            node->Action = action;
    }

    uint32_t BehaviorTree::AddNode(BehaviorKind kind, const std::string& name, glm::vec2 position)
    {
        if (m_NextID == UINT32_MAX)
            throw std::overflow_error("没有可用的节点编号。");
        uint32_t id = m_NextID++;
        m_Nodes.push_back({id, kind, name, {}, position});
        if (!m_Root)
            m_Root = id;
        return id;
    }

    void BehaviorTree::RemoveNode(uint32_t id)
    {
        m_Nodes.erase(std::remove_if(m_Nodes.begin(), m_Nodes.end(),
                                     [id](const BehaviorNode& node) { return node.ID == id; }),
                      m_Nodes.end());
        m_Edges.erase(std::remove_if(m_Edges.begin(), m_Edges.end(),
                                     [id](const BehaviorEdge& edge) { return edge.From == id || edge.To == id; }),
                      m_Edges.end());
        if (m_Root == id)
            m_Root = 0;
    }

    bool BehaviorTree::SetRoot(uint32_t id)
    {
        if (!Find(id))
            return false;
        m_Root = id;
        return true;
    }

    // ---- 连接与循环检查 ----

    bool BehaviorTree::Reaches(uint32_t from, uint32_t to) const
    {
        std::vector<uint32_t> pending{from};
        std::unordered_set<uint32_t> visited;
        while (!pending.empty())
        {
            uint32_t id = pending.back();
            pending.pop_back();
            if (id == to)
                return true;
            if (visited.insert(id).second)
                for (const BehaviorEdge& edge : m_Edges)
                    if (edge.From == id)
                        pending.push_back(edge.To);
        }
        return false;
    }

    bool BehaviorTree::Connect(uint32_t from, std::string key, uint32_t to, std::string& error)
    {
        const BehaviorNode* source = Find(from);
        if (!source || !Find(to))
            error = "连线的两端都需要节点。";
        else if (source->Kind == BehaviorKind::Action)
            error = "动作是这次决策的终点。";
        else if (source->Kind == BehaviorKind::ModelSelector && key.empty())
            error = "请填写模型返回的候选键。";
        else if (source->Kind == BehaviorKind::Subtree && !key.empty())
            error = "子树只有一个入口，无须候选键。";
        else if (Reaches(to, from))
            error = "这条连线会让执行回到先前的节点。";
        else
        {
            // 只换终点，画布上这一行的顺序保持不变。
            for (BehaviorEdge& edge : m_Edges)
            {
                if (edge.From == from && edge.Key == key)
                {
                    edge.To = to;
                    error.clear();
                    return true;
                }
            }
            m_Edges.push_back({from, to, std::move(key)});
            error.clear();
            return true;
        }
        return false;
    }

    void BehaviorTree::Disconnect(uint32_t from, std::string key)
    {
        m_Edges.erase(
            std::remove_if(m_Edges.begin(), m_Edges.end(),
                           [&](const BehaviorEdge& edge) { return edge.From == from && edge.Key == key; }),
            m_Edges.end());
    }

    bool BehaviorTree::Validate(std::string& error) const
    {
        if (!Find(m_Root))
        {
            error = "请指定行为树的根节点。";
            return false;
        }
        for (const BehaviorNode& node : m_Nodes)
        {
            bool connected = std::any_of(m_Edges.begin(), m_Edges.end(),
                                         [&](const BehaviorEdge& edge) { return edge.From == node.ID; });
            if (node.Kind == BehaviorKind::Action ? node.Action.empty() : !connected)
            {
                error =
                    node.Name + (node.Kind == BehaviorKind::Action ? "：请填写动作键。" : "：需要连接下一步。");
                return false;
            }
        }
        error.clear();
        return true;
    }

    // ---- 角色的执行路径 ----

    BehaviorTrace BehaviorTree::Tick(BehaviorBlackboard& actor, const std::string& candidate) const
    {
        BehaviorTrace trace;
        actor.Candidate = candidate;
        const BehaviorNode* node = Find(m_Root);
        while (node)
        {
            trace.Nodes.push_back(node->ID);
            if (node->Kind == BehaviorKind::Action)
            {
                trace.Action = node->Action;
                if (!trace.Action.empty())
                    actor.LastAction = trace.Action;
                break;
            }
            const BehaviorEdge* next = nullptr;
            for (const BehaviorEdge& edge : m_Edges)
            {
                if (edge.From != node->ID)
                    continue;
                if (node->Kind == BehaviorKind::Subtree || edge.Key == candidate)
                {
                    next = &edge;
                    break;
                }
                if (edge.Key == "*")
                    next = &edge;
            }
            if (!next)
                break;
            trace.Edges.push_back(*next);
            node = Find(next->To);
        }
        return trace;
    }

    // ---- 读取与保存 ----

    bool BehaviorTree::Load(const std::filesystem::path& path, std::string& error)
    {
        try
        {
            std::ifstream input(path);
            if (!input)
                throw std::runtime_error("打不开行为树：" + path.u8string());
            YAML::Node tree = YAML::Load(input);
            BehaviorTree loaded;
            loaded.m_Root = tree["Root"].as<uint32_t>();
            for (const YAML::Node& data : tree["Nodes"])
            {
                uint32_t id = data["ID"].as<uint32_t>();
                if (id == 0 || id == UINT32_MAX || loaded.Find(id))
                    throw std::runtime_error("节点编号无效或重复。");
                YAML::Node pos = data["Position"];
                BehaviorNode node{id,
                                  ReadKind(data["Kind"].as<std::string>()),
                                  data["Name"].as<std::string>(),
                                  data["Action"].as<std::string>(""),
                                  {pos[0].as<float>(), pos[1].as<float>()}};
                if (!std::isfinite(node.Position.x) || !std::isfinite(node.Position.y))
                    throw std::runtime_error("节点位置必须是有限数值。");
                loaded.m_Nodes.push_back(std::move(node));
                loaded.m_NextID = std::max(loaded.m_NextID, id + 1);
            }
            loaded.m_NextID = std::max(loaded.m_NextID, tree["NextID"].as<uint32_t>(1));
            for (const YAML::Node& data : tree["Edges"])
            {
                uint32_t from = data["From"].as<uint32_t>(), to = data["To"].as<uint32_t>();
                std::string key = data["Key"].as<std::string>("");
                const size_t count = loaded.m_Edges.size();
                if (!loaded.Connect(from, key, to, error))
                    return false;
                if (loaded.m_Edges.size() == count)
                    throw std::runtime_error("同一个候选键连接了两个节点。");
            }
            if (!loaded.Validate(error))
                return false;
            *this = std::move(loaded);
            return true;
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
            return false;
        }
    }

    bool BehaviorTree::Save(const std::filesystem::path& path, std::string& error) const
    {
        if (!Validate(error))
            return false;
        try
        {
            YAML::Node tree;
            tree["Root"] = m_Root;
            tree["NextID"] = m_NextID;
            for (const BehaviorNode& node : m_Nodes)
            {
                YAML::Node data;
                data["ID"] = node.ID;
                data["Kind"] = KindName(node.Kind);
                data["Name"] = node.Name;
                if (node.Kind == BehaviorKind::Action)
                    data["Action"] = node.Action;
                data["Position"].push_back(node.Position.x);
                data["Position"].push_back(node.Position.y);
                data["Position"].SetStyle(YAML::EmitterStyle::Flow);
                tree["Nodes"].push_back(data);
            }
            for (const BehaviorEdge& edge : m_Edges)
            {
                YAML::Node data;
                data["From"] = edge.From;
                data["To"] = edge.To;
                if (!edge.Key.empty())
                    data["Key"] = edge.Key;
                tree["Edges"].push_back(data);
            }
            FileSystem::WriteText(path, YAML::Dump(tree));
            return true;
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
            return false;
        }
    }
} // namespace Redflag
