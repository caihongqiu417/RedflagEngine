#pragma once

#include <iomanip>

// 用三块墙的颜色开关隔离真正同时经过 A、B 才到达 C 的光。
static void CheckProbeFeedback(Redflag::VulkanComputeDevice& device, const std::filesystem::path& folder,
                               RenderDocCapture& capture)
{
    using namespace Redflag;
    std::filesystem::create_directories(folder);
    std::ofstream rows(folder / "samples.csv");
    rows << std::setprecision(9);
    std::ofstream times(folder / "dispatch-and-wait.csv");
    times << "feedback,sample,render_ms\n";
    rows << "mode,reflectors,update,red,green,blue\n";
    constexpr uint32_t size = 65, center = size * (size / 2) + size / 2;
    const glm::vec3 eye(0, 1, 4);
    const auto camera = glm::perspective(glm::radians(44.f), 1.f, .1f, 15.f) *
        glm::lookAt(eye, glm::vec3(0, 1, -2), glm::vec3(0, 1, 0));
    auto wall = [](glm::vec3 p, glm::vec3 extent, glm::vec3 color) {
        return GIModel{{}, glm::scale(glm::translate(glm::mat4(1), p), extent), color};
    };
    // result[模式][开着的反光面][第1至4帧及静态烘焙]，中心像素固定落在墙 C。
    glm::vec3 result[2][4][5]{};
    for (int feedback = 0; feedback < 2; ++feedback)
    for (int mask = 0; mask < 4; ++mask)
    {
        std::vector<GIModel> walls = {
            wall({0, -.05, 0}, {4, .1, 4}, mask & 1 ? glm::vec3(.68,.12,.06) : glm::vec3(0)),
            wall({-2.05, 1, 0}, {.1, 2, 4}, mask & 2 ? glm::vec3(.63,.62,.58) : glm::vec3(0)),
            wall({0, 1, -2.05}, {4, 2, .1}, glm::vec3(.55))};
        GlobalIllumination gi(device, walls, size, size);
        gi.SetLights({{{-.5, 1.5, .1, 1}, {20,20,20,0}}});
        gi.SetCamera(camera, eye);
        gi.SetProbeFeedback(feedback != 0);
        std::vector<GISurface> surfaces(size * size);
        device.Readback(gi.GetSurfaces(), surfaces.data(), surfaces.size() * sizeof(GISurface));
        if (glm::length(glm::vec3(surfaces[center].Position) - glm::vec3(0,1,-2)) > .002f)
            throw std::runtime_error("The reference pixel missed the receiver wall C");
        std::vector<glm::vec4> pixels(size * size);
        const auto bake = folder / "three-walls.rfprobes";
        for (int frame = 0; frame < 5; ++frame)
        {
            if (frame == 4) gi.BakeProbes(bake);
            const bool record = mask == 3 && frame == 3;
            if (record) capture.Begin();
            gi.Render(GIMethod::DDGI, 64);
            if (record) capture.End();
            device.Readback(gi.GetOutput(), pixels.data(), pixels.size() * sizeof(glm::vec4));
            const auto rgb = glm::vec3(pixels[center]);
            result[feedback][mask][frame] = rgb;
            rows << feedback << ',' << mask << ',' << frame+1 << ',' << rgb.r << ',' << rgb.g << ',' << rgb.b << '\n';
            if (mask == 3 && (frame == 1 || frame == 4)) {
                const auto name = std::string(feedback ? "multi" : "single") + (frame == 4 ? "-baked" : "-live");
                std::ofstream image(folder / (name+".rgba32f"), std::ios::binary);
                image.write(reinterpret_cast<const char*>(pixels.data()), pixels.size()*sizeof(glm::vec4));
            }
        }
        gi.SetProbeFeedback(!feedback);
        if (gi.HasBakedProbes() || gi.LoadProbes(bake))
            throw std::runtime_error("A bake used a different feedback setting");
        gi.SetProbeFeedback(feedback != 0);
        if (!gi.LoadProbes(bake)) throw std::runtime_error("Matching feedback bake did not reload");
        gi.ResetHistory();
        gi.Render(GIMethod::DDGI);
        device.Readback(gi.GetOutput(), pixels.data(), pixels.size()*sizeof(glm::vec4));
        if (glm::vec3(pixels[center]) != result[feedback][mask][4])
            throw std::runtime_error("Reloading changed the receiver of the saved bake");

        if (mask == 3)
        {
            gi.SetSeed(139); // 更换种子会清掉已烘焙的探针，接下来测每帧实时更新。
            gi.SetSeed(137);
            for (int i = -4; i < 11; ++i) {
                const auto start = std::chrono::steady_clock::now();
                gi.Render(GIMethod::DDGI, 64);
                const auto ms = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now() - start).count();
                if (i >= 0) times << feedback << ',' << i+1 << ',' << ms << '\n';
            }
        }

        if (feedback && mask == 3)
        {
            // 几何和采样方向都不变。灯亮 32 倍，经过几次反射后也应该亮 32 倍。
            gi.SetLights({{{-.5, 1.5, .1, 1}, {640,640,640,0}}});
            for (int i = 0; i < 4; ++i) {
                if (i == 2) {
                    gi.SetCamera(glm::translate(glm::mat4(1), glm::vec3(.1,0,0)) * camera, eye);
                    gi.SetCamera(camera, eye);
                }
                gi.Render(GIMethod::DDGI, 64);
            }
            device.Readback(gi.GetOutput(), pixels.data(), pixels.size()*sizeof(glm::vec4));
            if (glm::length(glm::vec3(pixels[center]) - 32.f * result[1][3][3]) > .0001f)
                throw std::runtime_error("HDR light was clipped or camera motion restarted the room probes");
            std::cout << "HDR camera-motion check: " << pixels[center].r << " (expected "
                      << 32.f * result[1][3][3].r << ")\n";
        }
    }
    auto interaction = [&](int feedback, int frame) {
        const auto& x = result[feedback];
        return x[3][frame] - x[1][frame] - x[2][frame] + x[0][frame];
    };
    if (result[0][3][0] != result[1][3][0]) throw std::runtime_error("Cold-start probe feedback read uninitialized light");
    for (int i = 0; i < 5; ++i)
    {
        const auto single = interaction(0, i), multi = interaction(1, i);
        if (glm::length(single) > .00001f || (i && !(multi.r > .00001f)))
            throw std::runtime_error("Three-surface interaction did not match the number of bounces");
        std::cout << "Receiver C, update " << i+1 << ": joint A and B contribution in red " << single.r << " / " << multi.r << '\n';
    }
}
