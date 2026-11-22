#include "NpcAuthoringPanel.h"
#include "NpcAuthoringFiles.h"
#include "Redflag/Animation/ModelAnimation.h"
#include "imgui_internal.h"
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace Redflag;
namespace fs = std::filesystem;

static void Check(bool ok, const std::string& message)
{
    if (!ok)
        throw std::runtime_error(message);
}

void BeginPanelCapture();
void SavePanelCapture(const fs::path& path);

int main(int argc, char** argv)
{
    try
    {
        const bool poseCapture = argc == 3 && std::string(argv[1]) == "--pose-image";
        if (argc > 2 && (argc != 3 || (!poseCapture && std::string(argv[1]) != "--events-image")))
            throw std::runtime_error("Use [profile.png], --events-image events.png, or --pose-image pose.png");
        const fs::path data = "RedflagForge/assets/ai";
        const fs::path dir = fs::absolute("tmp/npc-authoring-panel");
        fs::create_directories(dir);
        for (const char* file : {"watt.npc.yaml", "watt.tree.yaml"})
            fs::copy_file(data / file, dir / file, fs::copy_options::overwrite_existing);
        if (fs::is_regular_file(data / "watt.animator.yaml"))
            fs::copy_file(data / "watt.animator.yaml", dir / "watt.animator.yaml",
                          fs::copy_options::overwrite_existing);
        std::string role = (dir / "watt.npc.yaml").u8string();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {640, 600};
        if (argc >= 2)
        {
            io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/msyh.ttc", 18.0f, nullptr,
                                         io.Fonts->GetGlyphRangesChineseFull());
            BeginPanelCapture();
        }
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        {
            NpcAuthoringPanel panel;
            const Ref<ModelAnimation> model = ModelAnimation::Load("RedflagForge/assets/game/station/crew/Operator.glb");
            Check(bool(model), "Could not load the operator model");
            auto frame = [&]() {
                ImGui::NewFrame();
                ImGui::SetNextWindowPos({0, 0});
                ImGui::SetNextWindowSize(io.DisplaySize);
                bool changed = panel.Draw(role, nullptr, {}, {}, model.get());
                ImGui::Render();
                return changed;
            };
            frame();
            frame();
            if (argc == 2)
                SavePanelCapture(argv[1]);
            ImGuiWindow* window = ImGui::FindWindowByName("角色创作");
            Check(window, "Missing authoring panel");
            const ImGuiID tab = ImHashStr("角色", 0, window->GetID("创作"));
            ImGui::ActivateItem(ImHashStr("提问时带上角色记下的事", 0, tab));
            frame();

            // 另一位作者先把 reserve 改成 share。当前面板只保存勾选过的角色记忆。
            BehaviorTree tree;
            std::string error;
            fs::path treeFile = dir / "watt.tree.yaml";
            Check(tree.Load(treeFile, error) && tree.Connect(1, "reserve", 5, error) && tree.Save(treeFile, error),
                  error);
            ImGui::ActivateItem(window->GetID("保存角色"));
            frame();
            bool saved = false;
            for (unsigned i = 0; i < 500 && !saved; ++i)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                saved = frame();
            }
            Check(saved, "Authoring panel did not save");
            NpcDefinition reopened;
            Check(reopened.Load(fs::u8path(role), error), error);
            Check(!reopened.Remember, "Checkbox was not saved");
            BehaviorBlackboard actor;
            Check(reopened.Tree.Tick(actor, "reserve").Action == "share", "Concurrent graph edit lost");

            // 键盘改动真实 InputFloat，再用面板按钮保存。和角色共用动作表的主角下次打开就读到新值。
            const ImGuiID animation = ImHashStr("动画事件", 0, window->GetID("创作"));
            ImGuiTabBar* bar = GImGui->TabBars.GetByKey(window->GetID("创作"));
            ImGui::TabBarQueueFocus(bar, ImGui::TabBarFindTabByID(bar, animation));
            frame();
            frame();
            const ImGuiID land = ImHashStr("land", 0, animation);
            frame();
            int recoveryRow = 1;
            const ImGuiID row = ImHashData(&recoveryRow, sizeof(recoveryRow), land);
            const ImGuiID end = ImHashStr("结束（秒）", 0, row);
            ImGui::ActivateItem(end);
            GImGui->NavNextActivateFlags = ImGuiActivateFlags_PreferInput;
            frame();
            Check(ImGui::GetActiveID() == end, "Recovery end field did not receive keyboard focus");
            io.AddInputCharactersUTF8("0.30");
            frame();
            io.AddKeyEvent(ImGuiKey_Enter, true);
            frame();
            io.AddKeyEvent(ImGuiKey_Enter, false);
            frame();
            const int editorVertices = ImGui::GetDrawData()->TotalVtxCount;
            ImGui::ActivateItem(ImHashStr("查看结束姿势", 0, row));
            frame();
            frame();
            Check(ImGui::GetDrawData()->TotalVtxCount > editorVertices + 100, "The pose button did not show both skeleton drawings");
            const auto& clips = model->GetClips();
            auto clip = std::find_if(clips.begin(), clips.end(), [](const auto& c) { return c.Name=="jump_5_hardlanding"; });
            Check(clip != clips.end(), "Missing operator landing clip");
            const auto savedPose = model->SampleBones(clip-clips.begin(), .20833333f);
            const auto editedPose = model->SampleBones(clip-clips.begin(), .30f);
            float difference = 0;
            for (size_t i=0; i<savedPose.size(); ++i)
                difference = std::max(difference, glm::distance(savedPose[i].second, editedPose[i].second));
            Check(savedPose.size()>10 && difference>.01f, "Two actual poses were expected to differ");
            std::cout << "PASS: actual GLB, " << savedPose.size() << " bone segments, largest change " << difference << " m\n";
            if (poseCapture) SavePanelCapture(argv[2]);
            ImGui::ActivateItem(ImHashStr("收起姿势对照", 0, animation));
            frame();
            frame();
            ImGui::ActivateItem(ImHashStr("保存动作表", 0, animation));
            frame();
            saved = false;
            for (unsigned i = 0; i < 700 && !saved; ++i)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                saved = frame();
            }
            Check(saved && reopened.Load(fs::u8path(role), error), "Animation events did not reopen: " + error);
            const auto& events = reopened.Controller.Find("land")->Events;
            Check(events.size() == 2 && events[1].End == .30f && events[0].Name == "KneeBend",
                  "Recovery window or knee marker was not saved");
            if (argc == 3 && !poseCapture)
            {
                io.DisplaySize = {800, 660};
                frame();
                ImGui::SetScrollY(window, window->ScrollMax.y);
                frame();
                SavePanelCapture(argv[2]);
            }
        }
        ImGui::DestroyContext();
        std::cout << "PASS: real authoring panel, toggle memory, native save/reopen, independent link edit survives\n"
                  << "PASS: edited Recovery.End from 0.20833333 to 0.30 with keyboard; saved, reloaded and retained KneeBend\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
