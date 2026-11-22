#pragma once

namespace RoughCheck
{
    constexpr int Size = 96;

    static double MeanSquaredDifference(const Image& a, const Image& b)
    {
        double sum = 0;
        for (size_t i = 0; i < a.size(); ++i)
        {
            glm::dvec3 difference = glm::dvec3(a[i]) - glm::dvec3(b[i]);
            sum += glm::dot(difference, difference);
        }
        return sum / (3 * a.size());
    }

    static double MeanRGB(const Image& image)
    {
        double sum = 0;
        for (const glm::vec4& p : image) sum += double(p.r) + p.g + p.b;
        return sum / (3 * image.size());
    }

    // 红色亮条中央与中间暗条取同样大小的区域。差值变小意味着条纹被抹平。
    static double StripeContrast(const Image& image)
    {
        double sum = 0;
        for (int y = 29; y <= 33; ++y)
            for (int x = 32; x < 64; ++x)
                sum += image[y * Size + x].r - image[(y + 17) * Size + x].r;
        return sum / (5 * 32);
    }
}

static void CheckRoughReflections(VulkanComputeDevice& device, const fs::path& out, RenderDocCapture& capture)
{
    using namespace RoughCheck;
    const glm::mat4 view(1);
    const glm::mat4 projection = glm::ortho(-1.f, 1.f, -1.f, 1.f, .01f, 15.f);
    GIModel mirror;
    mirror.Transform = glm::scale(glm::translate(view, {0, 0, -.55f}), {2.4f, 2.4f, .1f});
    mirror.MaterialFactors = {{.34f, 1.f}};
    GIModel sign;
    sign.Path = "RedflagForge/assets/models/LightPanel/LightPanel.gltf";
    sign.Transform = glm::rotate(glm::translate(view, {0, 0, 1}), glm::pi<float>(), {0, 1, 0});
    GlobalIllumination scene(device, std::vector<GIModel>{mirror, sign}, Size, Size);
    scene.SetCamera(projection, {0, 0, 0});
    ScreenEffects effects(device, Size, Size);
    Check(!effects.GetSettings().DenoiseReflections, "New callers must start with the original reflection");
    effects.GetSettings().TraceScene = true;

    const Image zero(Size * Size, glm::vec4(0));
    const size_t bytes = zero.size() * sizeof(glm::vec4);
    Ref<ComputeResource> base = device.CreateBuffer(bytes, zero.data());
    auto draw = [&](bool filter, uint32_t frame) {
        effects.GetSettings().DenoiseReflections = filter;
        Ref<ComputeResource> color = effects.Render(ScreenEffect::MaterialReflections, scene, projection, view, frame, base);
        Image result(zero.size());
        device.Readback(color, result.data(), bytes);
        return result;
    };

    // 参考图用第 256 至 767 组随机方向。前后各 256 次分别取平均，检查参考图自身还剩多少噪声。
    Image reference(zero), even(zero), odd(zero);
    for (uint32_t frame = 256; frame < 768; ++frame)
    {
        Image next = draw(false, frame);
        Image& half = frame < 512 ? even : odd;
        for (size_t i = 0; i < next.size(); ++i) half[i] += next[i] / 256.f;
    }
    for (size_t i = 0; i < zero.size(); ++i) reference[i] = .5f * (even[i] + odd[i]);
    Save(out / "test-reference.png", reference, Size, Size);

    // 测试用第 0 至 15 帧。开关滤波时输入完全相同，每个像素都只发一条反射光线。
    std::ofstream csv(out / "test-quality.csv");
    csv << "mode,width,height,frames,reference_spp,mean_rgb_mse,adjacent_rgb_mse,mean_linear_rgb,bright_minus_middle_red\n";
    const double referenceMean = MeanRGB(reference), referenceContrast = StripeContrast(reference);
    csv << "reference,96,96,1,512,0,0," << referenceMean << ',' << referenceContrast << '\n';
    const double referenceNoise = MeanSquaredDifference(even, odd) * .25;
    csv << "reference_split_estimate,96,96,2,256," << referenceNoise << ",,,\n";
    double rawMSE = 0, rawChange = 0;
    for (bool filter : {false, true})
    {
        double mse = 0, change = 0, mean = 0, contrast = 0;
        Image previous;
        for (uint32_t frame = 0; frame < 16; ++frame)
        {
            if (filter && frame == 0) capture.Begin();
            Image color = draw(filter, frame);
            if (filter && frame == 0) capture.End();
            if (frame == 0)
            {
                const char* name = filter ? "test-filtered" : "test-raw";
                Save(out / (std::string(name) + ".png"), color, Size, Size);
                std::ofstream raw(out / (std::string(name) + ".f32"), std::ios::binary);
                raw.write(reinterpret_cast<const char*>(color.data()), bytes);
            }
            mse += MeanSquaredDifference(color, reference) / 16.;
            if (frame > 0) change += MeanSquaredDifference(color, previous) / 15.;
            mean += MeanRGB(color) / 16.;
            contrast += StripeContrast(color) / 16.;
            previous = std::move(color);
        }
        csv << (filter ? "filtered" : "raw") << ",96,96,16,512," << mse << ',' << change << ',' << mean << ',' << contrast << std::endl;
        if (!filter) { rawMSE = mse; rawChange = change; }
        else
        {
            Check(mse < .65 * rawMSE && change < .65 * rawChange, "Rough reflection noise hardly fell");
            Check(std::abs(mean / referenceMean - 1) < .05, "Spatial filtering changed average reflected light");
            Check(referenceContrast > 0 && std::abs(contrast / referenceContrast - 1) < .20,
                  "Filtering erased the contrast between the white and gray lamp strips");
        }
    }

    // 邻近像素的直接光按红绿棋盘交替。倒影单独平滑后，再减去底图，结果应和黑色底图上的倒影相同。
    Image onBlack = draw(true, 7);
    Image checker(zero);
    for (int y = 0; y < Size; ++y)
        for (int x = 0; x < Size; ++x)
            checker[y * Size + x] = (x+y)%2 == 0 ? glm::vec4(1,.375,0,1) : glm::vec4(0,.125,2,1);
    device.Upload(base, checker.data(), bytes);
    Image onChecker = draw(true, 7);
    float baseError = 0;
    for (size_t i = 0; i < zero.size(); ++i)
        baseError = std::max(baseError, glm::length(glm::vec3(onChecker[i] - checker[i] - onBlack[i])));
    Check(baseError < 3e-5f, "Reflection filtering blurred the checkerboard in the base image");
    device.Upload(base, zero.data(), bytes);

    // 左半边始终保留同一块粗糙镜面。先让右半边留空，再放入另一块表面，检查左侧最靠近接缝的两列。
    std::vector<GISurface> original(zero.size());
    device.Readback(scene.GetSurfaces(), original.data(), original.size() * sizeof(GISurface));
    auto upload = [&](const std::vector<GISurface>& surfaces) {
        device.Upload(scene.GetSurfaces(), surfaces.data(), surfaces.size() * sizeof(GISurface));
    };
    std::vector<GISurface> surfaces(original);
    for (size_t i = 0; i < surfaces.size(); ++i)
        if (i % Size >= Size/2) surfaces[i].Position.w = 0;
    upload(surfaces);
    Image isolated = draw(true, 0), isolatedRaw = draw(false, 0);
    std::ofstream edges(out / "test-edges.csv");
    edges << "case,compared_pixels,max_linear_rgb_distance\nbase_checker," << zero.size() << ',' << baseError << '\n';
    for (const std::string kind : {"background", "smooth", "roughness", "depth", "normal", "metallic", "color"})
    {
        surfaces = original;
        for (size_t i = 0; i < surfaces.size(); ++i)
        {
            if (i % Size < Size/2) continue;
            GISurface& s = surfaces[i];
            if (kind == "background") s.Position.w = 0;
            else if (kind == "smooth") s.Normal.w = 0;
            else if (kind == "roughness") s.Normal.w = .85f;
            else if (kind == "depth") s.Position.z -= 1.f;
            else if (kind == "normal") s.Normal = {1, 0, 0, s.Normal.w};
            else if (kind == "metallic") s.Albedo.w = 0;
            else s.Albedo = {0.05f, .8f, .35f, s.Albedo.w};
        }
        upload(surfaces);
        Image filtered = draw(true, 0), raw = draw(false, 0);
        float error = 0;
        for (size_t i = 0; i < filtered.size(); ++i)
        {
            Check(filtered[i].a == raw[i].a, "Spatial filtering changed the center pixel hit flag");
            if (i % Size >= Size/2)
            {
                if (kind == "background" || kind == "smooth")
                    Check(filtered[i] == raw[i], "Filtering changed an empty pixel or an ideal mirror");
                continue;
            }
            if (i % Size < Size/2-2) continue;
            Check(raw[i] == isolatedRaw[i], "Boundary fixture changed the incoming ray on the measured side");
            error = std::max(error, glm::length(glm::vec3(filtered[i] - isolated[i])));
        }
        edges << kind << ',' << 2 * Size << ',' << error << std::endl;
        Check(error < .015f, "Material or geometry on the other side of the edge leaked into the mirror");
    }

    // 平移相机后，再建一个从没画过旧机位的视口。两者用同一组随机方向，整张新图应完全相同。
    upload(original);
    Image previous = draw(true, 7);
    const glm::vec3 eye(.35f, 0, 0);
    const glm::mat4 moved = glm::translate(view, -eye);
    scene.SetCamera(projection * moved, eye);
    ScreenEffects fresh(device, Size, Size);
    fresh.GetSettings() = effects.GetSettings();
    auto movedImage = [&](ScreenEffects& viewport) {
        Ref<ComputeResource> result = viewport.Render(ScreenEffect::MaterialReflections, scene, projection, moved, 7, base);
        Image color(zero.size());
        device.Readback(result, color.data(), bytes);
        return color;
    };
    Image afterMotion = movedImage(effects);
    Image firstView = movedImage(fresh);
    Check(MeanSquaredDifference(afterMotion, previous) > .001, "Moving the camera did not change the test image");
    Check(afterMotion == firstView, "A previous camera position left color in the spatial filter");
    edges << "camera_pan_vs_new_view," << afterMotion.size() << ",0\n";
}
