#include "rfpch.h"
#include "Platform/Vulkan/VulkanComputeDevice.h"
#include "Platform/Vulkan/VulkanContext.h"
#include <GLFW/glfw3.h>
#include <spdlog/sinks/ostream_sink.h>

using namespace Redflag;

static void VerifyHistory(ComputeDevice& device, const std::vector<uint32_t>& spirv)
{
    constexpr uint32_t width = 7, height = 5;
    std::array<uint32_t, width * height> samples{};
    std::array<float, width * height * 4> pixels{};
    for (uint32_t i = 0; i < samples.size(); ++i)
        samples[i] = i;

    auto data = device.CreateBuffer(sizeof(samples), samples.data());
    auto history = std::array{device.CreateImage(width, height, pixels.data()), device.CreateImage(width, height)};
    auto kernel = device.CreatePipeline(spirv.data(), spirv.size() * sizeof(uint32_t),
        {ComputeBinding::Buffer, ComputeBinding::Image, ComputeBinding::Image}, sizeof(uint32_t));

    // 三帧轮换两张图。第三帧需要读回第二帧写到第 0 张图中的值。
    for (uint32_t frame = 1; frame <= 3; ++frame)
        device.Dispatch(kernel, {data, history[(frame - 1) % 2], history[frame % 2]}, &frame,
            (width + 3) / 4, (height + 3) / 4);

    device.Readback(data, samples.data(), sizeof(samples));
    device.Readback(history[1], pixels.data(), sizeof(pixels));
    for (uint32_t i = 0; i < samples.size(); ++i)
    {
        if (samples[i] != i + 6 || pixels[4*i] != float(3*i + 10) || pixels[4*i+1] != float(3*(i % width))
            || pixels[4*i+2] != float(3*(i / width)) || pixels[4*i+3] != 3.f)
            throw std::runtime_error("Unexpected compute history at pixel " + std::to_string(i));
    }
    std::cout << "Three frames, 7x5 history and 35 buffer entries match. Last pixel R=" << pixels[136] << "\n";
}

int main(int argc, char** argv)
{
    if (argc != 2) return 1;
    Log::Init();
    std::ostringstream messages;
    auto errors = std::make_shared<spdlog::sinks::ostream_sink_mt>(messages);
    errors->set_level(spdlog::level::err);
    Log::GetCoreLogger()->sinks().push_back(errors);
    if (!glfwInit()) return 1;
    try
    {
        std::ifstream file(argv[1], std::ios::binary | std::ios::ate);
        auto bytes = static_cast<std::streamsize>(file.tellg());
        if (bytes <= 0 || bytes % 4) throw std::runtime_error("Missing or invalid compute shader");
        std::vector<uint32_t> code(static_cast<size_t>(bytes) / 4);
        file.seekg(0);
        file.read(reinterpret_cast<char*>(code.data()), bytes);

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        auto window = std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)>(
            glfwCreateWindow(128, 96, "RHI compute", nullptr, nullptr), glfwDestroyWindow);
        if (!window) throw std::runtime_error("Cannot create compute test window");
        VulkanContext context(window.get());
        context.Init();
        VulkanComputeDevice device(context);
        VerifyHistory(device, code);
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << "\n";
        glfwTerminate();
        return 1;
    }
    glfwTerminate();
    if (!messages.str().empty()) return 1;
    return 0;
}
