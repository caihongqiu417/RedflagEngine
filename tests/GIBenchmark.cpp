#include "rfpch.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Platform/Vulkan/VulkanComputeDevice.h"
#include "Redflag/Renderer/GI/GlobalIllumination.h"
#include "Redflag/Renderer/ScreenSpace/ScreenEffects.h"
#include <glm/gtc/matrix_transform.hpp>
#include "RenderDocCapture.h"
#include <GLFW/glfw3.h>
#include <filesystem>
#include "ProbeBakeCheck.h"
#include "ProbeSaveCheck.h"
#include "ProbeFeedbackCheck.h"
#include "EmissiveGICheck.h"
#include "TriangleIntersectionCheck.h"

using namespace Redflag;

// 正交镜头下，后方的方块和前方等大的方块应占同样宽度。
static void CheckOrthographic(VulkanComputeDevice &device)
{
    GIModel front, back, beyond;
    front.Transform = glm::translate(glm::mat4(1), glm::vec3(-1, 0, 0));
    back.Transform = glm::translate(glm::mat4(1), glm::vec3(1, 0, -4));
    beyond.Transform = glm::translate(glm::mat4(1), glm::vec3(0, 0, -14));
    GlobalIllumination gi(device, {front, back, beyond}, 16, 8);
    glm::vec3 eye(0, 0, 4);
    auto view = glm::lookAt(eye, glm::vec3(0), glm::vec3(0, 1, 0));
    gi.SetCamera(glm::ortho(-2.f, 2.f, -1.f, 1.f, .1f, 12.f) * view, eye);
    GISurface surface[16 * 8];
    device.Readback(gi.GetSurfaces(), surface, sizeof(surface));
    for (unsigned x = 0; x < 16; ++x)
    {
        const auto &p = surface[4 * 16 + x].Position;
        bool left = x >= 2 && x < 6, right = x >= 10 && x < 14;
        float worldX = -2.f + (float(x) + .5f) * .25f;
        if ((p.w > 0) != (left || right) ||
            ((left || right) && (std::abs(p.x - worldX) > .0001f || std::abs(p.z - (left ? .5f : -3.5f)) > .0001f)))
            throw std::runtime_error("Orthographic surface did not match projected cube or clipping distance");
    }
}

// 白墙的正面有两块三角形。落在对角线上的像素也应命中 z=0 的正面。
static void CheckSharedEdge(VulkanComputeDevice& device, const std::filesystem::path& folder)
{
    GIModel wall;
    wall.Transform = glm::scale(glm::translate(glm::mat4(1),glm::vec3(0,0,-.05)),glm::vec3(2.2,2.2,.1));
    GIModel side;
    side.Path = "tests/fixtures/EmissivePanel.gltf";
    side.Transform = glm::translate(glm::mat4(1),glm::vec3(1,0,1));
    const glm::vec3 eye(0,0,3);
    GlobalIllumination gi(device,std::vector<GIModel>{wall,side},17,17);
    gi.SetCamera(glm::ortho(-.25f,.25f,-.25f,.25f,.1f,8.f)*glm::lookAt(eye,glm::vec3(0),glm::vec3(0,1,0)),eye);
    std::vector<GISurface> wallPixels(17*17);
    device.Readback(gi.GetSurfaces(),wallPixels.data(),wallPixels.size()*sizeof(wallPixels[0]));
    std::filesystem::create_directories(folder);
    std::ofstream rows(folder/"pixels.csv");
    rows<<"x,y,hit_z\n";
    int missed=0;
    for (int i=0;i<17*17;++i)
    {
        rows<<i%17<<','<<i/17<<','<<wallPixels[i].Position.z<<'\n';
        if (wallPixels[i].Position.w==0 || std::abs(wallPixels[i].Position.z)>.0001f) ++missed;
    }
    if (missed) throw std::runtime_error(std::to_string(missed)+" wall pixels missed the shared edge");
    std::cout<<"Shared edge: all 289 pixels hit the front face.\n";
    std::ofstream directions(folder/"directions.csv");
    directions << "axis,side,projection,rays,misses\n";
    CheckWallDirections(device, directions);
    CheckOpeningAndShadow(device);
    CheckLongTriangle(device);
}

// 复现 Sponza-Atrium 的三盏编辑器灯。固定镜头，分别记录采样和亚像素偏移引起的帧间变化。
static void RunTemporal(VulkanComputeDevice &device, const std::filesystem::path &path,
                        const std::filesystem::path &out)
{
    constexpr uint32_t width = 320, height = 180, frames = 24;
    GIModel model;
    model.Path = path;
    model.Transform = glm::translate(glm::mat4(1), glm::vec3(.325295836f, -3.50185919f, .207966045f)) *
                      glm::scale(glm::mat4(1), glm::vec3(.671888709f));
    GlobalIllumination gi(device, std::vector<GIModel>{model}, width, height);
    gi.SetLights({{{-2, -1, 0, 1}, {15, 15, 15, 0}},
                  {{1.5f, 1.5f, -1, 1}, {28, 22.4f, 17.92f, 0}},
                  {{5.5f, -.5f, 1.2f, 1}, {12.16f, 14.63f, 19, 0}}});
    const glm::vec3 eye(-5.5f, -2.2f, 0);
    const auto view = glm::lookAt(eye, glm::vec3(6, -1.8f, 0), glm::vec3(0, 1, 0));
    const auto projection = glm::perspective(glm::radians(65.f), float(width) / height, .05f, 100.f);
    gi.SetCamera(projection * view, eye);
    std::filesystem::create_directories(out);
    std::ofstream indices(out / "temporal-history.csv");
    indices << "mode,frame,history_index\n";
    std::vector<glm::vec4> pixels(size_t(width) * height);
    auto run = [&](const char *name, GIMethod method, uint32_t samples, bool jitter) {
        gi.SetCamera(projection * view, eye);
        gi.SetSeed(137);
        std::ofstream file(out / (std::string("temporal-") + name + ".f32"), std::ios::binary);
        for (uint32_t frame = 0; frame < frames; ++frame)
        {
            auto camera = jitter ? ScreenEffects::JitterProjection(projection, frame, width, height) : projection;
            gi.SetCamera(camera * view, eye, false);
            gi.Render(method, samples);
            indices << name << ',' << frame + 1 << ',' << gi.GetFrameIndex() << '\n';
            device.Readback(gi.GetOutput(), pixels.data(), pixels.size() * sizeof(glm::vec4));
            file.write(reinterpret_cast<const char *>(pixels.data()), pixels.size() * sizeof(glm::vec4));
        }
        if (!file)
            throw std::runtime_error("Cannot save temporal samples");
    };
    run("direct", GIMethod::DirectReference, 1, false);
    auto fixedCamera = pixels;
    gi.Resize(width - 1, height - 1);
    gi.Render(GIMethod::DirectReference);
    gi.Resize(width, height);
    gi.Render(GIMethod::DirectReference);
    device.Readback(gi.GetOutput(), pixels.data(), pixels.size() * sizeof(glm::vec4));
    if (pixels != fixedCamera || gi.GetLights().size() != 3)
        throw std::runtime_error("Resize changed static camera output or scene lights");
    run("ddgi-2", GIMethod::DDGI, 2, false);
    run("ddgi-16", GIMethod::DDGI, 16, false);
    run("di", GIMethod::ReSTIRDI, 1, false);
    run("gi", GIMethod::ReSTIRGI, 1, false);
    run("direct-jitter", GIMethod::DirectReference, 1, true);
}

int main(int argc, char **argv)
{
    bool temporal = argc == 4 && std::string(argv[3]) == "--temporal";
    const bool bake = (argc == 4 || argc == 5) && std::string(argv[3]) == "--bake";
    const bool loadBake = (argc == 4 || argc == 5) && std::string(argv[3]) == "--load-bake";
    const bool feedback = (argc == 4 || argc == 5) && std::string(argv[3]) == "--probe-feedback";
    const bool emission = (argc == 4 || argc == 5) && std::string(argv[3]) == "--emission";
    const bool loadEmission = argc == 4 && std::string(argv[3]) == "--load-emission";
    const bool edge = argc == 4 && std::string(argv[3]) == "--triangle-edge";
    const bool failedBake = argc == 4 && std::string(argv[3]) == "--bake-failure";
    if (argc != 3 && !temporal && !bake && !loadBake && !feedback && !emission && !loadEmission && !edge && !failedBake && !(argc == 5 && std::string(argv[3]) == "--capture"))
    {
        std::cerr << "Usage: GIBenchmark model.gltf raw-output-directory "
                     "[--temporal | --capture renderdoc.dll | --bake | --load-bake | --bake-failure | --emission | --load-emission | --triangle-edge | --probe-feedback [renderdoc.dll]]\n";
        return 2;
    }
    RenderDocCapture capture(argc == 5 ? argv[4] : nullptr,
                             emission ? "output/captures/local/OffscreenEmitter" : feedback ? "output/captures/local/DDGI-Feedback" : bake ? "output/captures/local/DDGI-Live" : loadBake ? "output/captures/local/DDGI-Baked" : "output/profiling/gi/SponzaGI",
                             emission ? "Textured emitter outside the camera" : feedback ? "Three surfaces / probe feedback" : bake ? "Sponza / update and gather probes" : loadBake ? "Sponza / gather baked probes" : "Sponza / six GI dispatches",
                             emission ? "17x17 white receiver. No point lights. ReSTIR GI candidate and reuse in frame 512." : feedback ? "Fourth live update; the first capture disables feedback and the second enables it." : bake ? "Live DDGI probe update and gather before baking." : loadBake ? "Restored the saved probe data before this capture; gather only." :
                             "160x90. Probe update/gather, DI candidates/reuse, GI candidates/reuse.");
    Log::Init();
    if (!glfwInit())
        return 1;
    int status = 0;
    try
    {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)> window(
            glfwCreateWindow(64, 64, "GI benchmark", nullptr, nullptr), glfwDestroyWindow);
        if (!window)
            throw std::runtime_error("Cannot create GPU benchmark window");
        VulkanContext context(window.get());
        context.Init();
        VulkanComputeDevice device(context);
        if (failedBake)
            CheckProbeSaveFailure(device, std::filesystem::path(argv[2]) / "write-failure");
        else if (edge)
            CheckSharedEdge(device,std::filesystem::path(argv[2]) / "triangle-edge");
        else if (emission || loadEmission)
        {
            if (argc == 5)
            {
                if (!context.BeginFrame()) throw std::runtime_error("Cannot acquire capture window");
                context.BeginWindowPass();
                context.EndWindowPass();
                context.SwapBuffers();
            }
            CheckEmissiveGI(device, std::filesystem::path(argv[2]) / "emission",loadEmission,capture);
        }
        else if (feedback)
        {
            if (!context.BeginFrame()) throw std::runtime_error("Cannot acquire capture window");
            context.BeginWindowPass();
            context.EndWindowPass();
            context.SwapBuffers();
            CheckProbeFeedback(device, std::filesystem::path(argv[2]) / "probe-feedback", capture);
        }
        else if (bake || loadBake)
        {
            if (argc == 5)
            {
                if (!context.BeginFrame()) throw std::runtime_error("Cannot acquire capture window");
                context.BeginWindowPass();
                context.EndWindowPass();
                context.SwapBuffers();
            }
            CheckProbeBake(device, argv[1], std::filesystem::path(argv[2]) / "probe-bake", bake, capture);
        }
        else if (temporal)
        {
            CheckOrthographic(device);
            RunTemporal(device, argv[1], argv[2]);
        }
        else
        {
            constexpr uint32_t width = 160, height = 90;
            GlobalIllumination gi(device, argv[1], width, height);
            gi.SetProbeFeedback(false); // 参考图只算一次间接反射。这里也让探针只收集被灯直接照亮的表面反射来的光。
            if (argc == 5)
            {
                if (!context.BeginFrame())
                    throw std::runtime_error("Cannot acquire a capture window");
                context.BeginWindowPass();
                context.EndWindowPass();
                context.SwapBuffers();
                capture.Begin();
                gi.Render(GIMethod::DDGI, 16);
                gi.Render(GIMethod::ReSTIRDI);
                gi.Render(GIMethod::ReSTIRGI);
                capture.End();
            }
            else
            {
                CheckSharedEdge(device, std::filesystem::path(argv[2]) / "triangle-edge");
                const std::filesystem::path out = argv[2];
                std::filesystem::create_directories(out);
                VkPhysicalDeviceProperties gpu{};
                vkGetPhysicalDeviceProperties(context.GetPhysicalDevice(), &gpu);
                std::ofstream(out / "device.txt") << gpu.deviceName;
                auto dump = [&](const std::string &name, const Ref<ComputeResource> &resource, size_t bytes) {
                    std::vector<char> data(bytes);
                    device.Readback(resource, data.data(), bytes);
                    std::ofstream file(out / (name + ".f32"), std::ios::binary);
                    file.write(data.data(), data.size());
                    if (!file)
                        throw std::runtime_error("Cannot write " + name);
                };
                std::ofstream timing(out / "times.csv");
                timing << "image,frames,samples_per_frame,dispatch_and_wait_ms\n";
                auto run = [&](const char *name, GIMethod method, uint32_t samples, uint32_t frames, uint32_t seed) {
                    gi.SetSeed(seed);
                    auto start = std::chrono::steady_clock::now();
                    for (uint32_t i = 0; i < frames; i++)
                        gi.Render(method, samples);
                    double ms =
                        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                    std::string stem = std::string(name) + "-" + std::to_string(seed);
                    timing << stem << ',' << frames << ',' << samples << ',' << ms << '\n';
                    dump(stem, gi.GetOutput(), width * height * sizeof(glm::vec4));
                    std::cout << stem << " " << ms << " ms\n";
                };
                dump("surfaces", gi.GetSurfaces(), width * height * sizeof(GISurface));
                run("direct-reference", GIMethod::DirectReference, 1, 1, 5801);
                run("indirect-reference", GIMethod::IndirectReference, 512, 1, 5801);
                run("indirect-reference", GIMethod::IndirectReference, 512, 1, 9631);
                for (uint32_t seed : {137u, 293u, 587u, 811u})
                {
                    run("direct-1spp", GIMethod::Direct, 1, 1, seed);
                    run("direct-8spp", GIMethod::Direct, 8, 1, seed);
                    run("restir-di", GIMethod::ReSTIRDI, 1, 8, seed);
                    run("indirect-1spp", GIMethod::Indirect, 1, 1, seed);
                    run("indirect-8spp", GIMethod::Indirect, 8, 1, seed);
                    run("restir-gi", GIMethod::ReSTIRGI, 1, 8, seed);
                    run("ddgi", GIMethod::DDGI, 16, 8, seed);
                }
            }
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        status = 1;
    }
    glfwTerminate();
    return status;
}
