#include "rfpch.h"
#include "../RedflagForge/src/UI/DevContextPanel.h"
#include "Redflag/Core/Application.h"
#include "Redflag/Renderer/RendererAPI.h"
#include "imgui_internal.h"
#include <filesystem>
#include <thread>

using namespace Redflag;
namespace fs = std::filesystem;

int main(int argc, char** argv)
try
{
    if (argc != 4) throw std::runtime_error("Expected --dev-context-panel PROJECT TASK");
    const fs::path project = fs::absolute(fs::u8path(argv[2]));
    const std::string task = argv[3];
    const fs::path records = project / "captures/context" / task;
    _wputenv_s(L"REDFLAG_PROJECT", project.c_str());
    _putenv_s("REDFLAG_DEV_TASK", task.c_str());
    Log::Init();
    RendererAPI::SetAPI(RendererAPI::API::Vulkan);
    Application app("Forge development context tests", false);
    DevContextPanel panel;
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1500, 1100};
    io.DeltaTime = .05f;
    auto frame = [&] {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({1450, 1060});
        panel.Draw();
        ImGui::Render();
    };
    auto has = [&](const std::string& kind, const std::string& field, const std::string& value) {
        for (const auto& dir : fs::directory_iterator(records))
        {
            std::ifstream f(dir.path() / "event.json");
            if (!f) continue;
            const auto record = YAML::Load(f);
            if (record["kind"].as<std::string>("") == kind && record[field].as<std::string>("") == value) return true;
        }
        return false;
    };
    frame();
    ImGuiWindow* window = ImGui::FindWindowByName("开发资料");
    auto click = [&](const char* label) { ImGui::ActivateItem(window->GetID(label)); frame(); };
    auto until = [&](const char* message, const std::function<bool()>& check) {
        for (int i = 0; i < 700; ++i)
        {
            frame();
            if (check()) { std::cout << "PASS " << message << '\n'; return; }
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
        }
        throw std::runtime_error(message);
    };
    auto type = [&](const char* input, const char* words) {
        const ImGuiID id = window->GetID(input);
        until("text box receives keyboard input", [&] {
            ImGui::ActivateItem(id);
            GImGui->NavNextActivateFlags = ImGuiActivateFlags_PreferInput;
            frame();
            return ImGui::GetActiveID() == id;
        });
        io.AddInputCharactersUTF8(words);
        frame();
    };

    // 从实际的输入框提交勘误，再回到源码页。以同一份磁盘回执确认两次操作都已完成。
    const char* correction = "这次只核对 CreateMesh 的调用和测试，尚未测量 GPU 耗时。";
    type("##更正", correction);
    click("记下更正");
    until("note persisted in the shared task", [&] { return has("note", "text", correction); });
    until("switch to the code page after the task is reread", [&] {
        click("源码与测试");
        ImGui::ActivateItem(window->GetID("##源码查询"));
        GImGui->NavNextActivateFlags = ImGuiActivateFlags_PreferInput;
        frame();
        return ImGui::GetActiveID() == window->GetID("##源码查询");
    });
    click("把这次查询记入选中的任务");
    type("##源码查询", "CreateMesh");
    click("查找");
    until("find used the shared CLI and appended its references", [&] { return has("reference", "query", "CreateMesh"); });
    // 用 CLI 实际返回的第一个匹配定位 ImGui 列表项，重名时仍按路径区别。
    YAML::Node hit;
    for (const auto& dir : fs::directory_iterator(records))
    {
        std::ifstream f(dir.path() / "event.json");
        if (!f) continue;
        const auto row = YAML::Load(f);
        if (row["query"].as<std::string>("") == "CreateMesh") hit.reset(row["result"]["matches"][0]);
    }
    const std::string where = hit["source"].Scalar();
    auto retryAfter = std::chrono::steady_clock::now();
    until("selecting a match requested that source line", [&] {
        if (has("reference", "query", where)) return true;
        if (std::chrono::steady_clock::now() >= retryAfter)
        {
            const ImGuiID parent = window->GetID(0);
            ImGui::ActivateItem(ImHashStr(hit["symbol"].Scalar().c_str(), 0, parent));
            frame();
            retryAfter = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        }
        return false;
    });
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
