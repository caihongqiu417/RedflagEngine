#include "BehaviorTreePanel.h"
#include "imgui_internal.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace Redflag;
namespace fs = std::filesystem;

static void Check(bool ok, const std::string& message)
{
    if (!ok)
        throw std::runtime_error(message);
}

static void DrawPanel(BehaviorTreePanel& panel, BehaviorTree& tree, const fs::path& file)
{
    ImGui::NewFrame();
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({1400, 1000});
    panel.Draw(tree, file);
    ImGui::Render();
}

static void RewireInEditor(BehaviorTree& tree, const fs::path& file)
{
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = {1400, 1000};
    io.IniFilename = nullptr;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    BehaviorTreePanel panel;
    DrawPanel(panel, tree, file);
    DrawPanel(panel, tree, file);
    ImGuiWindow* canvas = nullptr;
    for (ImGuiWindow* w : ctx->Windows)
        if (std::strstr(w->Name, "BehaviorCanvas"))
            canvas = w;
    Check(canvas, "Behavior canvas missing");

    // 在面板实际绘制的位置按住 gather 的右端，再松到“修理设施”的输入端。
    float font = ctx->FontSize, row = font + ctx->Style.ItemSpacing.y, header = 2 * row + 6;
    glm::vec2 origin{canvas->Pos.x, canvas->Pos.y};
    glm::vec2 source = origin + tree.Find(1)->Position + glm::vec2(font * 9, header + row * .5f);
    glm::vec2 target = origin + tree.Find(9)->Position + glm::vec2(0, header * .5f);
    io.AddMousePosEvent(source.x, source.y);
    DrawPanel(panel, tree, file);
    io.AddMouseButtonEvent(0, true);
    DrawPanel(panel, tree, file);
    io.AddMousePosEvent(target.x, target.y);
    DrawPanel(panel, tree, file);
    io.AddMouseButtonEvent(0, false);
    DrawPanel(panel, tree, file);
    ImGui::DestroyContext(ctx);
}

int main()
{
    try
    {
        const fs::path source = "RedflagForge/assets/ai/watt.tree.yaml";
        const fs::path dir = "tmp/behavior-tree";
        fs::create_directories(dir);
        const fs::path file = dir / "saved.tree.yaml";
        BehaviorTree tree;
        std::string error;
        Check(tree.Load(source, error), error);
        BehaviorBlackboard watt, other;
        for (const std::string& key : {"gather", "share", "revive", "repair", "follow", "mark", "reserve", "report", "investigate", "look"})
            Check(tree.Tick(watt, key).Action == key, "Missing route " + key);
        BehaviorTrace initial = tree.Tick(watt, "gather");
        Check(initial.Nodes == std::vector<uint32_t>({1, 2, 3, 4}) && initial.Edges.size() == 3, "Subtree trace");
        other.Values["target"] = "crate";
        tree.Tick(other, "follow");
        Check(watt.LastAction == "gather" && watt.Values.empty() && other.LastAction == "follow",
              "Blackboards mixed");
        Check(tree.Tick(watt, "typo").Action.empty() && watt.LastAction == "gather", "Unknown candidate");
        Check(!tree.Connect(3, "gather", 1, error) && tree.Tick(watt, "gather").Action == "gather",
              "Cycle accepted");
        Check(!tree.Connect(4, "", 1, error) && !tree.Connect(1, "bad", 199, error), "Bad endpoints");

        RewireInEditor(tree, file);
        Check(tree.Tick(watt, "gather").Nodes == std::vector<uint32_t>({1, 9}),
              "Dragging did not change the route");
        tree.Move(9, {850, 360});
        Check(tree.Save(file, error), error);
        BehaviorTree reopened;
        Check(reopened.Load(file, error), error);
        Check(reopened.Tick(other, "gather").Action == "repair" &&
                  reopened.Find(9)->Position == glm::vec2(850, 360),
              "Round trip");
        Check(reopened.Tick(watt, "reserve").Nodes == std::vector<uint32_t>({1, 12}), "Reserve route");
        Check(reopened.Connect(1, "reserve", 5, error) && reopened.Save(file, error) && reopened.Load(file, error),
              error);
        Check(reopened.Tick(watt, "reserve").Action == "share" &&
                  reopened.Tick(other, "follow").Action == "follow",
              "Independent route");
        Check(reopened.Connect(1, "*", 10, error) && reopened.Tick(watt, "unknown").Action == "follow",
              "Default route");
        Check(reopened.Tick(watt, "gather").Action == "repair", "Default replaced the exact candidate");
        fs::path invalid = dir / "bad.tree.yaml";
        std::ofstream(invalid) << "Root: 1\nNodes: [{ID: 1, Kind: Subtree, Name: loop, Position: [0,0]}]\n"
                                  "Edges: [{From: 1, To: 1}]\n";
        Check(!reopened.Load(invalid, error) && reopened.Tick(watt, "gather").Action == "repair",
              "Failed reload lost graph");
        std::cout << "Seven routes, separate blackboards, drag gather -> repair, reserve -> share, save/reopen, "
                     "invalid edges: passed\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
