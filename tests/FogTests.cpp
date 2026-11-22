#include "rfpch.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Platform/Vulkan/VulkanComputeDevice.h"
#include "Redflag/Renderer/GI/GlobalIllumination.h"
#include "RenderDocCapture.h"
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <stb_image_write.h>

using namespace Redflag;
namespace fs = std::filesystem;
constexpr int Width = 384, Height = 216;
const fs::path Out = std::getenv("RF_FOG_OUTPUT") ? std::getenv("RF_FOG_OUTPUT") : "output/profiling/fog";

static GIModel Box(glm::vec3 center, glm::vec3 size)
{
    return { {}, glm::scale(glm::translate(glm::mat4(1), center), size), glm::vec3(.26f) };
}

static std::vector<glm::vec4> Read(VulkanComputeDevice& device, const Ref<ComputeResource>& buffer)
{
    std::vector<glm::vec4> pixels(Width * Height);
    device.Readback(buffer, pixels.data(), pixels.size() * sizeof(pixels[0]));
    return pixels;
}

static void Save(const char* file, const std::vector<glm::vec4>& pixels)
{
    std::vector<unsigned char> bytes(Width * Height * 3);
    for (size_t i = 0; i < pixels.size(); ++i)
        for (int c = 0; c < 3; ++c)
        {
            float x = pixels[i][c];
            if (!std::isfinite(x) || x < 0) throw std::runtime_error("Invalid linear fog color");
            bytes[3*i+c] = static_cast<unsigned char>(std::lround(255.f * std::pow(x / (1.f+x), 1.f/2.2f)));
        }
    if (!stbi_write_png((Out/file).string().c_str(), Width, Height, 3, bytes.data(), Width*3))
        throw std::runtime_error("Cannot write fog image");
}

static void Run(VulkanComputeDevice& device, RenderDocCapture& capture, bool recording)
{
    fs::create_directories(Out);
    const glm::vec3 eye{0,0,5};
    const glm::mat4 vp = glm::ortho(-4.f, 4.f, -2.25f, 2.25f, .1f, 16.f) *
                         glm::lookAt(eye, glm::vec3(0), glm::vec3(0,1,0));
    std::vector<GIModel> room = { Box({0,0,-4},{10,6,.2f}), Box({-2,2.25f,0},{.12f,4,10}),
        Box({-2,-2.25f,0},{.12f,4,10}), Box({-2,0,2.75f},{.12f,.5f,5}), Box({-2,0,-2.75f},{.12f,.5f,5}) };
    const std::vector<GILight> lights = {{{-3,0,0,1},{120,80,25,0}}};
    std::ofstream evidence;
    if (!recording) evidence.open(Out/"measurements.csv");
    evidence << "test,value\n";
    FogSettings fog;
    fog.Density = .16f; fog.Steps = 32; fog.Anisotropy = 0;

    GlobalIllumination gi(device, room, Width, Height);
    gi.SetCamera(vp, eye);
    std::vector<glm::vec4> white(Width*Height, glm::vec4(1));
    auto board = device.CreateBuffer(white.size()*sizeof(white[0]), white.data());
    auto clear = fog;
    clear.Density = 0;
    if (gi.RenderFog(board, clear) != board) throw std::runtime_error("Clear air changed the input");
    // 在没有灯的均匀雾里，每个像素只保留 exp(-密度 × 从近平面到表面的距离)。
    auto attenuation = Read(device, gi.RenderFog(board,fog));
    std::vector<GISurface> hit(Width*Height);
    device.Readback(gi.GetSurfaces(),hit.data(),hit.size()*sizeof(hit[0]));
    float largest = 0;
    for (size_t i = 0; i < hit.size(); ++i)
    {
        float distance = hit[i].Position.w ? 4.9f - hit[i].Position.z : 15.9f;
        largest = std::max(largest,std::abs(attenuation[i].r-std::exp(-fog.Density*distance)));
    }
    evidence << "absorption_max_error," << largest << '\n';
    if (largest > 2e-5f) throw std::runtime_error("Beer-Lambert test failed");

    gi.SetLights(lights);
    gi.RenderDirect(false);
    auto direct = gi.GetDirectOutput();
    Save("clear.png", Read(device,direct));
    if (recording) capture.Begin();
    auto lit = gi.RenderFog(direct,fog);
    if (recording) capture.End();
    const auto open = Read(device,lit);
    Save("open.png", open);
    if (recording) return;

    // 这条视线与灯水平相隔三单位。缩为一单位时，所有雾样本都回到关灯时的读数。
    const size_t center = Width*(Height/2) + Width/2;
    auto finite = lights;
    auto unlimited = Read(device,gi.RenderFog(board,fog));
    finite[0].Intensity.w = 1.f;
    gi.SetLights(finite);
    auto shortRange = Read(device,gi.RenderFog(board,fog));
    evidence << "infinite_range_center_red," << unlimited[center].r << '\n'
             << "one_unit_range_center_red," << shortRange[center].r << '\n'
             << "unlit_center_red," << attenuation[center].r << '\n';
    if (shortRange[center].r != attenuation[center].r || unlimited[center].r <= shortRange[center].r)
        throw std::runtime_error("Fog scattering ignored the point light radius");
    gi.SetLights(lights);

    for (int steps : {8,16,32})
    {
        fog.Steps = steps;
        for (int i = 0; i < 4; ++i) gi.RenderFog(direct,fog);
        std::vector<double> ms;
        for (int i = 0; i < 20; ++i)
        {
            auto start = std::chrono::steady_clock::now();
            gi.RenderFog(direct,fog);
            ms.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
        }
        std::sort(ms.begin(),ms.end());
        evidence << "fog_" << steps << "_steps_median_ms," << (ms[9]+ms[10])/2 << '\n';
    }

    // 封住小孔，右方同一个像素不再收到从灯穿过孔的散射光。
    room.push_back(Box({-2,0,0},{.12f,.5f,.5f}));
    GlobalIllumination closed(device, room, Width, Height);
    closed.SetCamera(vp,eye);
    closed.SetLights(lights);
    closed.RenderDirect(false);
    const auto shut = Read(device,closed.RenderFog(closed.GetDirectOutput(),fog));
    Save("closed.png",shut);
    size_t i = (Height/2)*Width + 3*Width/4;
    evidence << "open_pixel_red," << open[i].r << "\nclosed_pixel_red," << shut[i].r << '\n';
    if (open[i].r < .002f || shut[i].r > open[i].r*.02f) throw std::runtime_error("Volumetric occluder failed");
}

int main(int argc, char** argv)
{
    bool recording = argc == 2;
    RenderDocCapture capture(recording ? argv[1] : nullptr, "output/profiling/fog/Fog-Aperture",
                             "Light through an aperture", "One Vulkan volumetric fog dispatch, 32 steps.");
    Log::Init();
    if (!glfwInit()) return 1;
    int result = 0;
    try
    {
        glfwWindowHint(GLFW_CLIENT_API,GLFW_NO_API);
        glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
        std::unique_ptr<GLFWwindow,decltype(&glfwDestroyWindow)> window(
            glfwCreateWindow(64,64,"Fog test",nullptr,nullptr),glfwDestroyWindow);
        if (!window) throw std::runtime_error("Cannot open the Vulkan test window");
        VulkanContext context(window.get());
        context.Init();
        VulkanComputeDevice device(context);
        if (recording && context.BeginFrame())
        {
            context.BeginWindowPass();
            context.EndWindowPass();
            context.SwapBuffers();
        }
        Run(device,capture,recording);
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; result = 1; }
    glfwTerminate();
    return result;
}
