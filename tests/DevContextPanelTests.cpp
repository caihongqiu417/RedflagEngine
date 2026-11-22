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
    if (argc != 5) throw std::runtime_error("Expected --dev-context-panel PROJECT TASK COPIED_PROJECT");
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
    std::string rendered;
    io.ClipboardUserData = &rendered;
    io.SetClipboardTextFn = [](void* out, const char* text) { *static_cast<std::string*>(out) = text; };
    io.GetClipboardTextFn = [](void* out) { return static_cast<std::string*>(out)->c_str(); };
    auto frame = [&] {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({1450, 1060});
        ImGui::LogToClipboard();
        panel.Draw();
        ImGui::LogFinish();
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
        throw std::runtime_error(std::string(message) + "\n" + rendered.substr(0,1900));
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
    auto contains = [&](const char* words) { return rendered.find(words) != std::string::npos; };
    auto waitText = [&](const char* check, const char* words) { until(check,[&] {return contains(words);}); };

    waitText("failed check and stale source displayed separately", "源码已变化，需复测");
    if (!contains("测试失败") || !contains("测试通过")) throw std::runtime_error("missing check state");
    fs::path event;
    for (const auto& entry : fs::directory_iterator(records))
    {
        std::ifstream f(entry.path() / "event.json");
        if (f && YAML::Load(f)["exit_code"].as<int>(0) == 1) event = entry.path();
    }
    if (event.empty()) throw std::runtime_error("missing failed verification");
    auto checkAction = [&](const fs::path& base, const char* button) {
        const auto file = (base / event.filename() / "test.log").make_preferred().u8string();
        ImGui::ActivateItem(ImHashStr(button, 0, window->GetID(file.c_str())));
        frame();
    };
    checkAction(records, "原始测试输出");
    waitText("opened captured output in Forge", "triangle 000");
    click("下一页");
    waitText("read the original failure after line 80", "FAIL triangle needs 3 indices");
    click("返回资料");
    checkAction(records, "测试时的改动");
    waitText("read the saved patch in Forge", "+330");
    click("返回资料");
    click("查看当前完整改动");
    waitText("read complete working changes after the twenty-line preview", "+330");
    click("返回资料");

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
    waitText("source declaration ready","打开源码原文");
    click("打开源码原文");
    waitText("browsed the original source file","复制完整路径");
    click("返回资料");

    YAML::Node detail;
    for (const auto& row : fs::directory_iterator(records))
    {
        std::ifstream f(row.path()/"event.json");
        if (!f) continue;
        const auto entry = YAML::Load(f);
        if (entry["query"].as<std::string>("") == where) detail.reset(entry["result"]);
    }
    const std::string test = detail["tests"][0]["source"].Scalar();
    const int first = 0;
    ImGui::ActivateItem(ImHashStr(test.c_str(),0,ImHashData(&first,sizeof(first),window->GetID("测试"))));
    frame();
    until("opened a test location returning numbered lines", [&] {return has("reference","query",test);});
    waitText("test excerpt visible",test.c_str());
    click("打开源码原文");
    waitText("opened full test source", "复制完整路径");
    click("返回资料");

    // 在同一窗口换到移动后的记录副本。两处目录同名测试的原文都须可读。
    click("切换项目");
    if (GImGui->OpenPopupStack.empty()) throw std::runtime_error("missing project chooser");
    ImGuiWindow* mainWindow = window;
    window = GImGui->OpenPopupStack.back().Window;
    type("##项目目录", fs::absolute(fs::u8path(argv[4])).u8string().c_str());
    click("打开项目");
    window = mainWindow;
    until("changed project and discarded old selection", [&] { return contains("moved-project") && contains("选择任务"); });
    until("select task from moved records", [&] {
        if (!GImGui->OpenPopupStack.empty() && GImGui->OpenPopupStack.back().Window)
        {
            ImGuiWindow* list = GImGui->OpenPopupStack.back().Window;
            ImGui::ActivateItem(list->GetID(("检查网格索引与创建网格的调用   " + task).c_str()));
        }
        else ImGui::ActivateItem(window->GetID("任务"));
        frame();
        return contains(task.c_str()) && GImGui->OpenPopupStack.empty();
    });
    click("任务记录");
    waitText("copied task still contains failed check", "测试失败");
    checkAction(fs::absolute(fs::u8path(argv[4]))/"captures/context"/task, "原始测试输出");
    waitText("copy resolved the full test log under the new project", "triangle 000");
    if (!contains("moved-project")) throw std::runtime_error("still reading old project");
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
