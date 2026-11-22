#include "rfpch.h"
#include "../RedflagForge/src/UI/AssetChangeReview.h"
#include "../RedflagForge/src/UI/PlaytestReview.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Platform/Vulkan/VulkanDraw.h"
#include "Redflag/Core/Application.h"
#include "Redflag/Debug/PlaytestSession.h"
#include "Redflag/Renderer/RendererAPI.h"
#include "Redflag/Scene/Entity.h"
#include "imgui_internal.h"
#include <thread>

using namespace Redflag;
namespace fs = std::filesystem;

static YAML::Node Read(const fs::path& file)
{
    std::ifstream input(file);
    return YAML::Load(input);
}

static void Require(bool passed, const char* condition)
{
    if (!passed) throw std::runtime_error(condition);
    std::cout << "PASS " << condition << '\n';
}

int main(int argc, char** argv)
try
{
    if (argc != 5) throw std::runtime_error("Expected --review-panel PROJECT CHANGE SESSION");
    const fs::path project = fs::absolute(fs::u8path(argv[2]));
    const std::string change = argv[3], session = argv[4];
    const fs::path folder = project / "captures/playtests" / session;
    _wputenv_s(L"REDFLAG_PROJECT", project.c_str());
    _wputenv_s(L"REDFLAG_PLAYTEST", folder.c_str());

    Log::Init();
    RendererAPI::SetAPI(RendererAPI::API::Vulkan);
    Application app("Forge review controls", false);
    auto& context = static_cast<VulkanContext&>(app.GetWindow().GetGraphicsContext());
    VulkanDraw device(context);
    AssetChangeReview changes;
    changes.Select(change);
    PlaytestReview replay;
    replay.Select(session);
    auto game = PlaytestSession::Open(project / "assets/scenes/room.redflag");
    Require(game != nullptr, "open local playtest");
    Scene scene;
    Entity camera = scene.CreateEntityWithUUID(101, "主相机");
    camera.AddComponent<CameraComponent>();
    game->BeginFrame(.016);
    game->RecordScene(scene);

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1600, 1200};
    io.DeltaTime = .1f;
    bool inPlay = false;
    auto frame = [&]() {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("审查验收");
        if (inPlay)
            replay.Draw(context, device);
        else
            changes.Draw();
        ImGui::End();
        ImGui::Render();
    };
    auto wait = [&](const auto& check) {
        for (int i = 0; i < 300; ++i)
        {
            frame();
            if (check()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(12));
        }
        return false;
    };
    frame();
    frame();
    ImGuiWindow* window = ImGui::FindWindowByName("审查验收");
    auto click = [&](const char* name) {
        ImGui::ActivateItem(window->GetID(name));
        frame();
    };
    const fs::path file = project / "assets/presentation.yaml";
    const fs::path record = project / "captures/changes" / change / "change.json";

    // 人改说明文字，工具改亮度。应用和撤销都从当前磁盘内容开始。
    click("应用到项目文件");
    Require(wait([&] { return Read(record)["state"].Scalar() == "applied"; }), "real Apply button completes");
    Require(Read(file)["Brightness"].as<int>() == 30 && Read(file)["Caption"].Scalar() == "人工修改",
            "apply preserves the author's other field");
    // Python 已写文件时，按钮可能仍在等子进程退出。等界面恢复后再撤销。
    Require(wait([&] {
                if (Read(record)["state"].Scalar() == "undone") return true;
                ImGui::ActivateItem(window->GetID("撤销这次修改"));
                return false;
            }),
            "real Undo button completes");
    Require(Read(file)["Brightness"].as<int>() == 18 && Read(file)["Caption"].Scalar() == "人工修改",
            "undo restores only Brightness");
    for (int i = 0; i < 20; ++i)
    {
        frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(12));
    }
    {
        std::ofstream output(file);
        output << "Brightness: 21\nCaption: 人工修改\n";
    }
    click("应用到项目文件");
    for (int i = 0; i < 90; ++i)
    {
        frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    Require(Read(record)["state"].Scalar() == "undone" && Read(file)["Brightness"].as<int>() == 21,
            "conflicting same field is preserved");

    inPlay = true;
    frame();
    frame();
    click("接管操作");
    Require(Read(folder / "handoff.json")["owner"].Scalar() == "human", "human takes over in the panel");
    frame();
    click("交给 AI");
    Require(Read(folder / "handoff.json")["owner"].Scalar() == "agent", "panel hands control back to the agent");

    // 通过实际输入控件给已选中的实体写一条批注。
    click("批注对象");
    frame();
    ImGuiWindow* popup = ImGui::FindWindowByName("##Combo_00");
    Require(popup && popup->Active, "actor selector opened");
    ImGui::ActivateItem(popup->GetID("主相机  101"));
    frame();
    const ImGuiID comment = window->GetID("##试玩意见");
    ImGui::ActivateItem(comment);
    GImGui->NavNextActivateFlags = ImGuiActivateFlags_PreferInput;
    frame();
    Require(ImGui::GetActiveID() == comment, "comment text box receives keyboard input");
    io.AddInputCharactersUTF8("画面中主相机保持原位。");
    frame();
    click("保存到这一帧");
    Require(wait([&] { return fs::exists(folder / "feedback.jsonl"); }), "panel writes frame feedback");
    const auto note = Read(folder / "feedback.jsonl");
    Require(note["seq"].as<int>() == 1 && note["entity"].Scalar() == "101" &&
                note["text"].Scalar() == "画面中主相机保持原位。",
            "feedback retains the selected frame, actor and text");
    // 同一段 2.16 秒的台词在维修站中途被打断。检查设备只读到当时记录的那一部分。
    const fs::path recording = "../output/validation/playtest-audio/station/speech-7-1102-4.wav";
    std::ifstream wav(recording,std::ios::binary);
    Require(bool(wav),"open original interrupted station recording");
    int64_t heard = -1;
    SpeechPlayback speaker;
    speaker.PlayRecording({std::istreambuf_iterator<char>(wav),{}},14700,[&](const SpeechPlayback::Event& event) {
        if (std::string(event.Name)=="finished") heard=event.DeviceSamples;
    });
    Require(wait([&]{ speaker.Update(); return !speaker.Playing(); }) && heard==14700,"replay ends after the recorded 14700 device samples");
    context.WaitIdle();
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
