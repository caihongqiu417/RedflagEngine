#pragma once

// 第一次运行把探针存到文件。重新启动后，再用这些探针照亮场景，与第一次保存的画面对照。
static void CheckProbeBake(Redflag::VulkanComputeDevice& device, const std::filesystem::path& model,
                           const std::filesystem::path& output, bool write, RenderDocCapture& capture)
{
    using namespace Redflag;
    constexpr uint32_t width = 160, height = 90;
    std::filesystem::create_directories(output);
    const auto bake = output / "static.rfprobes";
    GlobalIllumination gi(device, model, width, height);
    gi.SetProbeFeedback(false); // 与保存的 Sponza 单次漫反射截帧使用相同设置。
    if (write)
    {
        capture.Begin();
        gi.Render(GIMethod::DDGI, 16);
        capture.End();
        gi.BakeProbes(bake);
    }
    else if (!gi.LoadProbes(bake))
        throw std::runtime_error("Saved probes did not match after a process restart");

    if (!write) capture.Begin();
    gi.Render(GIMethod::DDGI);
    if (!write) capture.End();
    std::vector<glm::vec4> pixels(width * height);
    const size_t bytes = pixels.size() * sizeof(pixels[0]);
    device.Readback(gi.GetOutput(), pixels.data(), bytes);
    if (std::none_of(pixels.begin(), pixels.end(), [](glm::vec4 p) { return p.r > .001f; }))
        throw std::runtime_error("Baked image has no indirect light");
    auto reference = output / "indirect.f32";
    if (write)
    {
        std::ofstream out(reference, std::ios::binary);
        out.exceptions(std::ios::badbit | std::ios::failbit);
        out.write(reinterpret_cast<const char*>(pixels.data()), bytes);
        return;
    }

    std::vector<glm::vec4> expected(pixels.size());
    std::ifstream in(reference, std::ios::binary);
    if (!in.read(reinterpret_cast<char*>(expected.data()), bytes) || std::memcmp(expected.data(), pixels.data(), bytes))
        throw std::runtime_error("Reload changed the indirect HDR output");

    GlobalIllumination original(device, model, width, height);
    original.SetProbeFeedback(false);
    std::ofstream times(output / "dispatch-and-wait.csv");
    times << "round,mode,milliseconds\n";
    for (int round = 0; round < 4; ++round)
    {
        bool baked = round == 1 || round == 2;
        auto& method = baked ? gi : original;
        for (int i = -4; i < 16; ++i)
        {
            auto start = std::chrono::steady_clock::now();
            method.Render(GIMethod::DDGI, 16);
            double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            if (i >= 0) times << round << ',' << (baked ? "baked" : "live") << ',' << elapsed << '\n';
        }
    }
    gi.Resize(63, 47);
    gi.SetCamera(glm::lookAt(glm::vec3(1, 2, 3), glm::vec3(0), glm::vec3(0, 1, 0)), {1, 2, 3});
    gi.Render(GIMethod::DDGI);
    if (!gi.HasBakedProbes()) throw std::runtime_error("Moving the camera invalidated world-space probes");
    auto lights = gi.GetLights();
    std::reverse(lights.begin(), lights.end());
    gi.SetLights(lights);
    if (!gi.LoadProbes(bake)) throw std::runtime_error("Saving the same lights in reverse order invalidated the bake");
    lights.front().Intensity *= .5f;
    gi.SetLights(lights);
    if (gi.HasBakedProbes() || gi.LoadProbes(bake))
        throw std::runtime_error("Old light intensity was accepted");

    GlobalIllumination other(device, std::vector<GIModel>{GIModel{}}, 16, 9);
    other.SetProbeFeedback(false);
    other.SetLights(original.GetLights());
    if (other.LoadProbes(bake)) throw std::runtime_error("Wrong geometry was accepted");

    GIModel left, right;
    left.EntityID = 11; right.EntityID = 12;
    left.Transform[3][0] = -2.f; right.Transform[3][0] = 2.f;
    left.Tint = {1,.3f,.3f};
    const auto boxesFile = output / "two-boxes.rfprobes";
    GlobalIllumination boxes(device, std::vector<GIModel>{left, right}, 16, 9);
    boxes.SetLights(lights);
    boxes.BakeProbes(boxesFile);
    GlobalIllumination reopened(device, std::vector<GIModel>{right, left}, 16, 9);
    reopened.SetLights(lights);
    if (!reopened.LoadProbes(boxesFile)) throw std::runtime_error("Saved entity order invalidated the bake");
    std::cout << "Restart: " << pixels.size() << " HDR pixels identical. Camera and resize reused probes. "
              << "Reordered lights and models accepted. Changed lights and geometry rejected.\n";
}
