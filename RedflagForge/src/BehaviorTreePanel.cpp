#include "rfpch.h"
#include "BehaviorTreePanel.h"
#include "Redflag/Scripting/NpcScriptAction.h"
#include <imgui.h>

namespace Redflag
{
    static const char* NodeLabel(BehaviorKind kind)
    {
        switch (kind)
        {
        case BehaviorKind::ModelSelector:
            return "模型选择";
        case BehaviorKind::Subtree:
            return "子树";
        case BehaviorKind::Action:
            return "动作";
        }
        return "";
    }

    // ---- 工具栏与节点选择 ----

    void BehaviorTreePanel::Select(const BehaviorNode& node)
    {
        m_Selected = node.ID;
        std::snprintf(m_Name, sizeof(m_Name), "%s", node.Name.c_str());
        std::snprintf(m_Action, sizeof(m_Action), "%s", node.Action.c_str());
    }

    void BehaviorTreePanel::Draw(BehaviorTree& tree, const std::filesystem::path& asset,
                                 const BehaviorTrace* trace)
    {
        if (!ImGui::Begin("行为树"))
        {
            ImGui::End();
            return;
        }
        if (ImGui::Button("保存"))
        {
            if (tree.Save(asset, m_Message))
                m_Message = "已保存 " + asset.filename().u8string();
        }
        ImGui::SameLine();
        if (ImGui::Button("重新打开") && tree.Load(asset, m_Message))
        {
            m_Selected = m_From = 0;
            m_ShowPreview = false;
            m_Message = "已读入 " + asset.filename().u8string();
        }
        ImGui::SameLine();
        if (ImGui::Button("添加节点"))
            ImGui::OpenPopup("NodeMenu");
        if (ImGui::BeginPopup("NodeMenu"))
        {
            for (BehaviorKind kind : {BehaviorKind::ModelSelector, BehaviorKind::Subtree, BehaviorKind::Action})
            {
                if (ImGui::MenuItem(NodeLabel(kind)))
                {
                    uint32_t id = tree.AddNode(kind, NodeLabel(kind), glm::vec2(36, 40) - m_Pan);
                    Select(*tree.Find(id));
                }
            }
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("回到根节点"))
        {
            if (const BehaviorNode* root = tree.Find(tree.GetRoot()))
                m_Pan = glm::vec2(32, 80) - root->Position;
        }
        ImGui::SetNextItemWidth(125);
        ImGui::InputText("候选键", m_Key, sizeof(m_Key));
        ImGui::SameLine();
        if (ImGui::Button("试跑"))
        {
            BehaviorBlackboard actor;
            m_Preview = tree.Tick(actor, m_Key);
            m_ShowPreview = true;
            m_Message = m_Preview.Action.empty() ? "这个候选没有连到动作。" : "执行动作：" + m_Preview.Action;
        }
        if (m_ShowPreview && trace)
        {
            ImGui::SameLine();
            if (ImGui::Button("显示角色的执行路径"))
                m_ShowPreview = false;
        }
        ImGui::TextDisabled("用鼠标中键平移画布。从节点右侧圆点拖到下一节点的左侧圆点。");
        DrawCanvas(tree, m_ShowPreview ? &m_Preview : trace);
        DrawNodeSettings(tree);
        if (!m_Message.empty())
            ImGui::TextWrapped("%s", m_Message.c_str());
        ImGui::End();
    }

    // ---- 画布与连线 ----

    void BehaviorTreePanel::DrawCanvas(BehaviorTree& tree, const BehaviorTrace* trace)
    {
        const float height = std::max(160.f, ImGui::GetContentRegionAvail().y * .64f);
        ImGui::BeginChild("BehaviorCanvas", {0, height}, true,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        if (ImGui::IsWindowHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
        {
            ImVec2 delta = ImGui::GetIO().MouseDelta;
            m_Pan += glm::vec2(delta.x, delta.y);
        }
        ImVec2 origin = ImGui::GetWindowPos(), mouse = ImGui::GetMousePos();
        const float row = ImGui::GetTextLineHeightWithSpacing(), width = ImGui::GetFontSize() * 9.f;
        const float header = row * 2.f + 6.f;
        auto location = [&](const BehaviorNode& node) {
            return ImVec2(origin.x + m_Pan.x + node.Position.x, origin.y + m_Pan.y + node.Position.y);
        };
        ImDrawList* draw = ImGui::GetWindowDrawList();
        auto port = [&](const BehaviorNode& node, const std::string& key) {
            ImVec2 p = location(node);
            if (node.Kind == BehaviorKind::Subtree)
                return ImVec2(p.x + width, p.y + header * .5f);
            int index = 0;
            for (const BehaviorEdge& edge : tree.GetEdges())
            {
                if (edge.From != node.ID)
                    continue;
                if (edge.Key == key)
                    break;
                ++index;
            }
            return ImVec2(p.x + width, p.y + header + (index + .5f) * row);
        };
        auto wire = [&](ImVec2 a, ImVec2 b, ImU32 color, float thickness) {
            float reach = std::max(32.f, std::abs(b.x - a.x) * .4f);
            draw->AddBezierCubic(a, {a.x + reach, a.y}, {b.x - reach, b.y}, b, color, thickness);
        };
        const ImU32 active = IM_COL32(252, 188, 105, 255), line = IM_COL32(149, 156, 167, 255);
        for (const BehaviorEdge& edge : tree.GetEdges())
        {
            ImVec2 a = port(*tree.Find(edge.From), edge.Key), b = location(*tree.Find(edge.To));
            b.y += header * .5f;
            bool used = trace && std::any_of(trace->Edges.begin(), trace->Edges.end(), [&](const BehaviorEdge& e) {
                            return e.From == edge.From && e.To == edge.To && e.Key == edge.Key;
                        });
            wire(a, b, used ? active : line, used ? 3.f : 1.5f);
        }
        if (const BehaviorNode* from = tree.Find(m_From))
        {
            wire(port(*from, m_FromKey), mouse, active, 2.f);
        }
        // 把线拖到另一个节点左侧的圆点，再松手换到新节点。
        const bool released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
        for (const BehaviorNode& node : tree.GetNodes())
        {
            ImVec2 p = location(node);
            float boxHeight =
                node.Kind == BehaviorKind::ModelSelector ? port(node, "").y - p.y + row * .5f : header;
            ImVec2 end{p.x + width, p.y + boxHeight}, input{p.x, p.y + header * .5f};
            bool visited =
                trace && std::find(trace->Nodes.begin(), trace->Nodes.end(), node.ID) != trace->Nodes.end();
            draw->AddRectFilled(p, end, IM_COL32(49, 49, 55, 255), 5.f);
            draw->AddRect(p, end, node.ID == m_Selected ? IM_COL32(203, 83, 93, 255) : (visited ? active : line),
                          5.f, 0, 2.f);
            draw->PushClipRect(p, end, true);
            std::string kind =
                node.ID == tree.GetRoot() ? std::string("根 · ") + NodeLabel(node.Kind) : NodeLabel(node.Kind);
            draw->AddText({p.x + 12, p.y + 2}, visited ? active : line, kind.c_str());
            draw->AddText({p.x + 12, p.y + 2 + ImGui::GetTextLineHeightWithSpacing()}, IM_COL32_WHITE,
                          node.Name.c_str());
            draw->PopClipRect();
            draw->AddCircleFilled(input, 5.f, line);

            ImGui::PushID(int(node.ID));
            ImGui::SetCursorScreenPos({p.x + 9, p.y});
            ImGui::InvisibleButton("node", {width - 18, boxHeight});
            if (ImGui::IsItemClicked())
                Select(node);
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            {
                ImVec2 delta = ImGui::GetIO().MouseDelta;
                tree.Move(node.ID, node.Position + glm::vec2(delta.x, delta.y));
            }
            auto outputPort = [&](const std::string& key, const char* label) {
                ImVec2 output = port(node, key);
                draw->AddCircleFilled(output, 6.f, active);
                if (label)
                    draw->AddText({p.x + 12, output.y - row * .5f}, line, label);
                ImGui::SetCursorScreenPos({output.x - 8, output.y - 8});
                ImGui::PushID(key.c_str());
                ImGui::InvisibleButton("out", {16, 16});
                if (ImGui::IsItemActivated())
                {
                    Select(node);
                    m_From = node.ID;
                    m_FromKey = label && key.empty() ? m_Key : key;
                }
                ImGui::PopID();
            };
            if (node.Kind == BehaviorKind::Subtree)
                outputPort("", nullptr);
            if (node.Kind == BehaviorKind::ModelSelector)
            {
                for (const BehaviorEdge& edge : tree.GetEdges())
                    if (edge.From == node.ID)
                        outputPort(edge.Key, edge.Key.c_str());
                outputPort("", "+ 候选键");
            }
            if (m_From && released &&
                ImGui::IsMouseHoveringRect({input.x - 10, input.y - 10}, {input.x + 10, input.y + 10}))
            {
                if (tree.Connect(m_From, m_FromKey, node.ID, m_Message))
                {
                    m_ShowPreview = false;
                    m_Message = m_FromKey.empty() ? "已连接子树入口。" : "已连接候选 " + m_FromKey;
                }
                m_From = 0;
            }
            ImGui::PopID();
        }
        if (released)
            m_From = 0;
        ImGui::EndChild();
    }

    // ---- 节点属性 ----

    void BehaviorTreePanel::DrawNodeSettings(BehaviorTree& tree)
    {
        const BehaviorNode* node = tree.Find(m_Selected);
        if (!node)
            return;
        if (ImGui::InputText("节点名称", m_Name, sizeof(m_Name)))
            tree.Rename(node->ID, m_Name);
        if (node->Kind == BehaviorKind::Action && ImGui::InputText("动作键", m_Action, sizeof(m_Action)))
            tree.SetAction(node->ID, m_Action);
        if (node->Kind == BehaviorKind::Action && ImGui::BeginCombo("项目脚本", "选择 C# 动作"))
        {
            for (const std::string& name : NpcScriptAction::Names())
                if (ImGui::Selectable(name.c_str(), node->Action == name))
                {
                    tree.SetAction(node->ID, name);
                    Select(*node);
                }
            ImGui::EndCombo();
        }
        if (ImGui::Button("设为根节点"))
            tree.SetRoot(node->ID);
        ImGui::SameLine();
        if (ImGui::Button("删除节点"))
        {
            tree.RemoveNode(m_Selected);
            m_Selected = 0;
            m_ShowPreview = false;
            return;
        }
        for (const BehaviorEdge& edge : tree.GetEdges())
        {
            if (edge.From != node->ID)
                continue;
            ImGui::PushID(edge.Key.c_str());
            ImGui::Text("%s → %s", edge.Key.empty() ? "进入子树" : edge.Key.c_str(),
                        tree.Find(edge.To)->Name.c_str());
            ImGui::SameLine();
            bool remove = ImGui::SmallButton("断开");
            ImGui::PopID();
            if (remove)
            {
                std::string key = edge.Key;
                tree.Disconnect(node->ID, key);
                m_ShowPreview = false;
                break;
            }
        }
    }
} // namespace Redflag
