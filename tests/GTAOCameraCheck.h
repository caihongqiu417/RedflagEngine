#pragma once

// 镜头沿箱子前面平移。物体和地面固定，地面同一点的半球遮挡有独立的射线参考。
static void CheckGTAOCamera(VulkanComputeDevice& device, const fs::path& output, RenderDocCapture& capture)
{
    constexpr uint32_t width = 320, height = 180, count = width * height;
    GIModel ground, box, post;
    auto place = [](glm::vec3 center, glm::vec3 size) {
        return glm::scale(glm::translate(glm::mat4(1), center), size);
    };
    ground.Transform = place({0,-.05f,0}, {12,.1f,12});
    box.Transform = place({0,.5f,0}, {1,1,1});
    post.Transform = place({1.05f,.5f,0}, {.12f,1,.12f});
    GlobalIllumination scene(device, std::vector<GIModel>{ground,box,post}, width, height);
    ScreenEffects effects(device, width, height);
    std::vector<glm::vec4> colors(count, glm::vec4(1));
    Ref<ComputeResource> white = device.CreateBuffer(count * sizeof(colors[0]), colors.data());
    std::vector<GISurface> surfaces(count);
    std::vector<float> pixels(count * 5);

    for (const std::string mode : {"perspective", "taa", "ortho"})
    {
        bool taa = mode == "taa";
        glm::mat4 projection = mode == "ortho" ? glm::ortho(-2.4f,2.4f,-1.35f,1.35f,.05f,80.f)
            : glm::perspective(glm::radians(44.f), float(width)/height, .05f, 80.f);
        fs::path folder = output / mode;
        fs::create_directories(folder);
        std::ofstream matrices(folder / "cameras.csv");
        matrices.precision(10);
        effects.ResetHistory();
        for (uint32_t frame = 0; frame <= 12; ++frame)
        {
            glm::vec3 eye(-.9f + .15f*frame, 1.65f, 3.8f);
            glm::mat4 view = glm::lookAt(eye, glm::vec3(.15f,.25f,0), glm::vec3(0,1,0));
            glm::mat4 raster = taa ? effects.JitterProjection(projection,frame,width,height) : projection;
            glm::mat4 vp = raster * view;
            scene.SetCamera(vp, eye);
            if (mode == "perspective" && frame == 6) capture.Begin();
            Ref<ComputeResource> ao = effects.Render(ScreenEffect::GTAO, scene, raster, view, frame, white);
            if (mode == "perspective" && frame == 6) capture.End();
            device.Readback(ao, colors.data(), count * sizeof(colors[0]));
            device.Readback(scene.GetSurfaces(), surfaces.data(), count * sizeof(surfaces[0]));
            for (uint32_t i = 0; i < count; ++i)
            {
                const GISurface& surface = surfaces[i];
                pixels[5*i] = surface.Position.x;
                pixels[5*i+1] = surface.Position.w ? surface.Position.y : -100.f;
                pixels[5*i+2] = surface.Position.z;
                pixels[5*i+3] = colors[i].a;
            }
            if (taa)
            {
                Ref<ComputeResource> resolved = effects.Render(ScreenEffect::TAA, scene, raster, view, frame, ao, &projection);
                device.Readback(resolved, colors.data(), count * sizeof(colors[0]));
            }
            for (uint32_t i = 0; i < count; ++i) pixels[5*i+4] = colors[i].r;
            for (int r = 0; r < 4; ++r)
                for (int c = 0; c < 4; ++c) matrices << vp[c][r] << ((r==3 && c==3) ? '\n' : ',');
            std::ofstream file(folder / (std::to_string(frame)+".f32"), std::ios::binary);
            file.write(reinterpret_cast<const char*>(pixels.data()), pixels.size()*sizeof(float));
            if (!file) throw std::runtime_error("Cannot save GTAO camera sweep");
        }
    }
}
