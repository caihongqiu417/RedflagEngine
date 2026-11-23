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
    int gatherRow = 0;
    for (const BehaviorEdge& edge : tree.GetEdges()) {
        if (edge.From != 1) continue;
        if (edge.Key == "gather") break;
        ++gatherRow;
    }
    glm::vec2 source = origin + tree.Find(1)->Position + glm::vec2(font * 9, header + row * (gatherRow+.5f));
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

static void CheckSentinelRules()
{
    BehaviorTree tree;
    std::string error;
    Check(tree.Load("RedflagForge/assets/ai/sentinel.tree.yaml",error),error);
    const char* facts[] = {"damaged", "visible", "outside_reach", "cooling", "ready", "last_seen"};
    for (int mask = 0; mask < 64; ++mask) {
        BehaviorBlackboard input;
        for (int bit = 0; bit < 6; ++bit)
            if (mask & (1 << bit)) input.Values[facts[bit]] = "true";
        const char* expected = "patrol";
        if (mask & 1) expected = "damaged";
        else if (mask & 2) {
            if (mask & 4) expected = "chase";
            else if (mask & 8) expected = "cool";
            else if (mask & 16) expected = "discharge";
            else expected = "charge";
        } else if (mask & 32) expected = "search";
        Check(tree.Tick(input, "").Action == expected, "incorrect priority for mask " + std::to_string(mask));
        Check(tree.Tick(input, "damaged").Action == expected, "unrelated model choice overrides the physical facts");
    }

    // 两条条件同时成立时，调整边的顺序应该改变执行结果，并随行为树文件一起保存。
    BehaviorBlackboard actor;
    actor.Values = {{"visible", "true"}, {"cooling", "true"}, {"ready", "true"}};
    Check(tree.Tick(actor, "").Action == "cool", "original cooling branch has priority");
    Check(tree.RaisePriority(3, "ready"), "could not move ready above cooling");
    Check(tree.Tick(actor, "").Action == "discharge", "raised ready branch did not win");
    const fs::path copy = "tmp/behavior-tree/sentinel-priority.tree.yaml";
    fs::create_directories(copy.parent_path());
    BehaviorTree reopened;
    Check(tree.Save(copy, error) && reopened.Load(copy, error), error);
    Check(reopened.Tick(actor, "").Action == "discharge", "saved and reopened branch order changed");
    std::cout << "PASS sentinel priority for 64 combinations and unrelated model text; "
              << "raising ready above cooling persists after saving and reopening\n";
}

int main()
{
    try
    {
        CheckSentinelRules();
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
        BehaviorTrace live;
        tree.Advance(watt, "gather", live);
        Check(live.Waiting == 3 && live.Action.empty() && tree.Offers(3,"gather") && !tree.Offers(3,"repair"),
              "A nested question receives only its connected options");
        tree.Advance(watt, "share", live);
        Check(!live.Waiting && live.Action == "share" && live.Nodes == std::vector<uint32_t>({1,2,3,5}),
              "A different second answer resumes exactly once from the supply node");
        tree.Tick(watt,"gather");
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
                  reopened.Find(9)->Position == glm::vec2(850, 360) && reopened.Find(3)->Question == tree.Find(3)->Question,
              "Round trip");
        Check(reopened.Tick(watt, "reserve").Nodes == std::vector<uint32_t>({1, 2, 3, 12}), "Reserve route");
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
