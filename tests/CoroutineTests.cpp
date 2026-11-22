#include "rfpch.h"
#include "Redflag/Core/Application.h"
#include "Redflag/Renderer/RendererAPI.h"
#include "Redflag/Scene/Entity.h"
#include <filesystem>
#include <thread>

using namespace Redflag;

int main(int argc, char** argv)
{
    if (argc != 3) return 1;
    std::filesystem::current_path(std::filesystem::u8path(argv[2]));
    Log::Init();
    RendererAPI::SetAPI(RendererAPI::API::Vulkan);
    Application app("Script coroutine tests", false);
    JobSystem& jobs = app.GetJobs();
    jobs.Trace(true);
    Scene scene;
    Entity author = scene.CreateEntityWithUUID(1,"Author");
    author.AddComponent<ScriptComponent>().ClassName = "Workshop.CoroutineProbe";
    Entity transient = scene.CreateEntityWithUUID(2,"Transient");
    transient.AddComponent<ScriptComponent>().ClassName = "Workshop.Unfinished";
    scene.CreateEntityWithUUID(3,"Listener").AddComponent<ScriptComponent>().ClassName = "Workshop.Unfinished";
    scene.OnRuntimeStart();

    // 暂不执行工作线程提交的主线程回调，在读文件的实体删除之后才交付。
    scene.UpdateGame(.25f);
    scene.UpdateGame(.25f);
    scene.DestroyEntity(transient);
    const auto bytes = std::filesystem::file_size("routine-events.txt");
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    if (std::filesystem::file_size("routine-events.txt") != bytes) return 2;
    for (int frame = 0; frame < 70; ++frame)
    {
        jobs.Pump();
        scene.UpdateGame(.25f);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (author.GetComponent<TransformComponent>().Translation != glm::vec3(2,3,5)) return 3;
    scene.OnRuntimeStop();
    jobs.Drain();
    for (const JobTiming& row : jobs.TakeTimings())
    {
        if (row.Name != "Read script text asset" && row.Name != "Script text ready") continue;
        std::cout << "JOB " << row.ID << " " << (row.Lane == JobThread::Main ? "main" : "worker")
                  << " " << row.Thread << " " << (row.Dependencies.empty() ? 0 : row.Dependencies[0]) << "\n";
    }
    std::cout << "PASS paused scene did not advance a script; main-thread continuation wrote its transform\n";
    return 0;
}
