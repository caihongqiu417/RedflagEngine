#pragma once

// 左半幅与太阳之间有一块挡板。上下两排表面分别距相机 3 和 11 个单位，两处的入射方向与强度应当相同。
static void CheckDirectionalLight(VulkanComputeDevice& device, const fs::path& output, RenderDocCapture& capture)
{
    constexpr int width = 48, height = 16, count = width * height;
    const glm::mat4 view(1), projection = glm::ortho(-3.f, 3.f, -1.f, 1.f, .1f, 20.f);
    const glm::vec3 radiance(2.f, 1.f, .5f), base(.8f, .5f, .2f);
    GIModel blocker;
    blocker.Transform = glm::scale(glm::translate(view, {-1.5f, 0.f, 7.f}), {3.f, 4.f, .1f});
    blocker.Tint = base;
    blocker.MaterialFactors = {{1.f, 0.f}};
    GlobalIllumination scene(device, std::vector<GIModel>{blocker}, width, height);
    scene.SetRasterCamera(projection, glm::vec3(0), true);
    ScreenEffects effects(device, width, height);
    std::vector<GISurface> wall(count);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            glm::vec3 position((x + .5f) * 6.f / width - 3.f, 1.f - (y + .5f) * 2.f / height,
                               y < height/2 ? -3.f : -11.f);
            wall[y * width + x] = {glm::vec4(position, 1), {0,0,1,1}, glm::vec4(base, 0)};
        }
    device.Upload(scene.GetSurfaces(), wall.data(), count * sizeof(GISurface));
    std::vector<glm::vec4> pixels(count, glm::vec4(0));
    const Ref<ComputeResource> black = device.CreateBuffer(count * sizeof(pixels[0]), pixels.data());
    fs::create_directories(output);
    std::ofstream csv(output / "directional.csv");
    csv << "case,method,left_near_red,right_near_red,left_far_red,right_far_red,max_abs_rgb_error\n";

    auto compare = [&](const char* name, const char* method, const Ref<ComputeResource>& result,
                       const std::vector<glm::vec3>& expected) {
        device.Readback(result, pixels.data(), count * sizeof(pixels[0]));
        float error = 0;
        for (int i = 0; i < count; ++i)
            for (int c = 0; c < 3; ++c)
            {
                if (!std::isfinite(pixels[i][c])) throw std::runtime_error(std::string(method) + ": non-finite RGB");
                error = std::max(error, std::abs(pixels[i][c] - expected[i][c]));
            }
        csv << name << ',' << method;
        for (int y : {height/4, 3*height/4})
            for (int x : {width/4, 3*width/4}) csv << ',' << pixels[y*width+x].r;
        csv << ',' << error << '\n';
        if (error > 2.e-5f) // 单精度 GPU 与 CPU 公式逐通道比较；在此光强下，容许 0.00002 的线性 RGB 差。
            throw std::runtime_error(std::string(name) + " / " + method + ": max RGB error " + std::to_string(error));
    };

    // 正对粗糙度为 1 的非金属墙时，glTF 镜面项为 0.04/(4π)，漫反射项为底色 × 0.96/π。
    const glm::vec3 lit = radiance * (.96f * base + .01f) / glm::pi<float>();
    FogSettings fog;
    fog.Density = .15f;
    fog.Albedo = .8f;
    fog.Anisotropy = .4f;
    std::vector<glm::vec3> surface(count), mist(count);
    for (int setup = 0; setup < 3; ++setup)
    {
        const bool shadows = setup == 1, backlit = setup == 2;
        const char* name = shadows ? "sun_shadow_on" : backlit ? "sun_reversed" : "sun_shadow_off";
        scene.SetLights({GILight::Directional({0, 0, backlit ? 2.f : -2.f}, radiance, shadows)});
        // 镜头看向 -Z。反转太阳后，观察方向与指向太阳的方向重合，雾朝镜头散射的光增多。
        const float g = fog.Anisotropy, cosine = backlit ? 1.f : -1.f;
        const float phase = (1-g*g) / (4 * glm::pi<float>() * std::pow(1+g*g-2*g*cosine, 1.5f));
        for (int i = 0; i < count; ++i)
        {
            const bool blocked = shadows && wall[i].Position.x < 0;
            surface[i] = blocked || backlit ? glm::vec3(0) : lit;
            const float distance = -wall[i].Position.z - .1f;
            mist[i] = blocked ? glm::vec3(0) : radiance * (fog.Albedo * phase * -std::expm1(-fog.Density * distance));
        }

        if (shadows) capture.Begin();
        scene.RenderDirect(false);
        compare(name, "all", scene.GetDirectOutput(), surface);
        compare(name, "clustered", effects.Render(ScreenEffect::ClusteredLighting, scene, projection, view, 0), surface);
        scene.RenderDirect(true);
        compare(name, "restir_di", scene.GetDirectOutput(), surface);
        compare(name, "fog", scene.RenderFog(black, fog), mist);
        if (shadows) capture.End();
    }

    // 点光源离上排墙更近。分簇剔除应保留所有表面的太阳，同时只让半径内的表面收到点光。
    const GILight point{{0, 0, -2, 1}, {.4f, .6f, .8f, 3.f}};
    scene.SetLights({GILight::Directional({0,0,-1}, radiance, false), point});
    for (int i = 0; i < count; ++i)
        surface[i] = lit + glm::vec3(ReferencePoint(wall[i], point, {0,0,1}));
    scene.RenderDirect(false);
    compare("sun_and_point", "all", scene.GetDirectOutput(), surface);
    compare("sun_and_point", "clustered", effects.Render(ScreenEffect::ClusteredLighting, scene, projection, view, 0), surface);

    // 将屏幕上的墙换成白色平面镜。反射光线朝 +Z 前进，只在左半幅碰到相机后面的挡板。
    for (GISurface& s : wall)
    {
        s.Position.z = -1.f;
        s.Normal.w = 0.f;
        s.Albedo = glm::vec4(1);
    }
    device.Upload(scene.GetSurfaces(), wall.data(), count * sizeof(GISurface));
    effects.GetSettings().TraceScene = true;
    for (bool litFace : {false, true})
    {
        scene.SetLights({GILight::Directional({0, 0, litFace ? 1.f : -1.f}, radiance, false)});
        // 挡板朝镜子的那一面法线为 -Z，正面受光时仍可用上面的正入射公式。
        for (int i = 0; i < count; ++i)
            surface[i] = litFace && wall[i].Position.x < 0 ? lit : glm::vec3(0);
        const Ref<ComputeResource> result = effects.Render(ScreenEffect::MaterialReflections, scene, projection, view, 0, black);
        compare(litFace ? "mirror_frontlit" : "mirror_backlit", "scene_reflection", result, surface);
        for (int i = 0; i < count; ++i)
            if (pixels[i].a != (wall[i].Position.x < 0 ? 1.f : 0.f))
                throw std::runtime_error("Mirror hit mask disagrees with the board silhouette");
    }
}
