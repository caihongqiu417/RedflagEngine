#pragma once

// 镜面在相机前方，灯牌和挡板都放在相机背后。相机只看得到镜子，灯牌只能沿反射方向找到。
static void CheckSceneReflections(VulkanComputeDevice& device, const fs::path& out, RenderDocCapture* capture)
{
    constexpr int size = 96;
    const glm::mat4 projection = glm::ortho(-1.f, 1.f, -1.f, 1.f, .01f, 15.f);
    const glm::vec3 eye(0);
    const glm::mat4 view(1);
    ScreenEffects effects(device, size, size);
    Image black(size * size, glm::vec4(0));
    const Ref<ComputeResource> input = device.CreateBuffer(black.size() * sizeof(black[0]), black.data());
    const Ref<ComputeResource> sky = EnvironmentLighting::Load(device, "bin-int/Tests/test-room.rfibl");
    Check(bool(sky), "Missing constant HDR sky for reflection checks");
    Check(!effects.GetSettings().TraceScene, "Screen-only reflection must remain the default");

    GIModel mirror;
    mirror.Transform = glm::scale(glm::translate(view, {0, 0, -.55f}), {2.4f, 2.4f, .1f});
    mirror.MaterialFactors = {{0, 1}}; // 白色、完全光滑的金属，正面位于 z = -0.5。
    GIModel sign;
    sign.Path = "RedflagForge/assets/models/LightPanel/LightPanel.gltf";
    sign.Transform = glm::rotate(glm::translate(view, {0, 0, 1}), glm::pi<float>(), {0, 1, 0});

    auto world = [&](const std::vector<GIModel>& models) {
        Scope<GlobalIllumination> scene = CreateScope<GlobalIllumination>(device, models, size, size);
        scene->SetCamera(projection, eye);
        return scene;
    };
    auto draw = [&](GlobalIllumination& scene, uint32_t frame = 0) {
        const Ref<ComputeResource> image = effects.Render(ScreenEffect::MaterialReflections, scene,
                                                          projection, view, frame, input);
        Image result(black.size());
        device.Readback(image, result.data(), result.size() * sizeof(result[0]));
        return result;
    };
    std::ofstream csv(out / "scene-reflections.csv");
    csv << "case,x,y,r,g,b,hit\n";
    auto sample = [&](const char* name, const Image& image, int x, int y) {
        glm::vec4 p = image[y * size + x];
        csv << name << ',' << x << ',' << y << ',' << p.r << ',' << p.g << ',' << p.b << ',' << p.a << '\n';
        return glm::vec3(p);
    };

    Scope<GlobalIllumination> room = world({mirror, sign});
    std::vector<GISurface> visible(black.size());
    device.Readback(room->GetSurfaces(), visible.data(), visible.size() * sizeof(visible[0]));
    for (const GISurface& s : visible)
        Check(std::abs(s.Position.z + .5f) < .0001f && s.Normal.z > .99f && s.Albedo.w == 1,
              "Initial camera saw something other than the front of the mirror");

    Image screen = draw(*room);
    Check(sample("screen_only", screen, 48, 31) == glm::vec3(0), "Invisible sign leaked into the screen-only image");
    effects.GetSettings().TraceScene = true;
    if (capture) capture->Begin();
    Image local = draw(*room);
    if (capture)
    {
        capture->End();
        std::ofstream raw(out / "mirror.f32", std::ios::binary);
        raw.write(reinterpret_cast<const char*>(local.data()), local.size() * sizeof(local[0]));
    }
    // 这三个像素分别对着贴图里的白条、中间的灰条和黑边。红通道最亮处为线性亮度 4。
    glm::vec3 bright = sample("white_stripe", local, 48, 31);
    glm::vec3 gray = sample("gray_stripe", local, 48, 48);
    glm::vec3 dark = sample("black_frame", local, 48, 14);
    float linearGray = std::pow((128.f / 255.f + .055f) / 1.055f, 2.4f);
    const glm::vec3 emission(4, .48f, .14f);
    Check(glm::length(bright - emission) < .005f && glm::length(gray - linearGray * emission) < .005f &&
          glm::length(dark) < .001f, "Reflected glTF stripes differ from their linear RGB values");
    Save(out / "mirror-sign.png", local, size, size);
    // 固定镜头也固定像素位置。零粗糙度的镜子只有一个反射方向，灯牌的边缘每一帧都应落在同一处。
    float maximumChange = 0.f;
    for (uint32_t frame = 1; frame < 16; ++frame)
    {
        Image next = draw(*room, frame);
        for (size_t i = 0; i < local.size(); ++i)
            maximumChange = std::max(maximumChange, glm::length(glm::vec3(next[i] - local[i])));
    }
    std::ofstream stability(out / "mirror-stability.csv");
    stability << "width,height,frames,max_linear_rgb_distance_from_frame0\n"
              << size << ',' << size << ",16," << maximumChange << '\n';
    Check(maximumChange == 0.f, "Zero-roughness mirror changed across frames with a fixed camera");

    effects.SetEnvironment(sky);
    Image withSky = draw(*room);
    glm::vec3 skyOutside = sample("beside_sign_sky", withSky, 89, 31);
    effects.GetSettings().TraceReflections = false;
    Image skyOnly = draw(*room);
    Check(skyOutside.r > .5f && skyOutside == sample("sky_only", skyOnly, 89, 31), "A missed scene ray lost the sky");
    Check(glm::length(sample("ssr_off_at_sign", skyOnly, 48, 31) - skyOutside) < .001f,
          "Scene tracing remained active with SSR off");
    effects.GetSettings().TraceReflections = true;
    effects.GetSettings().ReflectionDistance = .25f;
    Check(sample("lamp_out_of_range", draw(*room), 48, 31) == glm::vec3(skyOnly[31 * size + 48]),
          "Scene reflection searched past the selected distance");
    effects.GetSettings().ReflectionDistance = 12.f;

    GIModel board;
    board.Transform = glm::scale(glm::translate(view, {0, 0, .46f}), {1.4f, 1.6f, .12f});
    board.Tint = glm::vec3(0);
    board.MaterialFactors = {{1, 1}};
    Scope<GlobalIllumination> blocked = world({mirror, board, sign});
    Image foreground = draw(*blocked);
    Check(glm::length(sample("black_board_before_lamp", foreground, 48, 31)) < .001f,
          "Black foreground exposed the bright sky or the lamp behind it");
    Save(out / "mirror-board.png", foreground, size, size);

    effects.SetEnvironment({});
    GIModel behind = board;
    behind.Transform = glm::translate(view, {0, 0, 1.2f}) * board.Transform;
    Scope<GlobalIllumination> behindLamp = world({mirror, sign, behind});
    Check(sample("board_behind_lamp", draw(*behindLamp), 48, 31) == bright, "A farther opaque surface replaced the lamp");
    GIModel glass = board;
    glass.TransparentParts = {1};
    Scope<GlobalIllumination> throughGlass = world({mirror, glass, sign});
    Check(sample("thin_transparent_board", draw(*throughGlass), 48, 31) == bright, "Thin glass blocked the reflected lamp");
    GIModel reversed = sign;
    reversed.Transform = glm::translate(view, {0, 0, 1});
    Scope<GlobalIllumination> backFace = world({mirror, reversed});
    Check(sample("unlit_back_of_lamp", draw(*backFace), 48, 31) == glm::vec3(0), "Single-sided lamp emitted backwards");

    // 镜中的绿色挡板由右侧的点光源照亮。把小方块放在灯和板之间，它能遮住灯，又挡不到朝前看的镜面。
    board.Tint = {.02f, .8f, .02f};
    board.MaterialFactors = {{.8f, 0}};
    const GILight lamp{{.84f, 0, .05f, 1}, {8, 8, 8, 0}};
    Scope<GlobalIllumination> greenBoard = world({mirror, board, sign});
    greenBoard->SetLights({lamp});
    glm::vec3 lit = sample("point_lit_board", draw(*greenBoard), 48, 48);
    Check(lit.g > .15f && lit.g > 5 * lit.r, "Reflection lost the illuminated material at the second surface");
    GIModel shade;
    shade.Transform = glm::scale(glm::translate(view, {.43f, 0, .225f}), {.16f, .3f, .14f});
    Scope<GlobalIllumination> shadow = world({mirror, board, shade, sign});
    shadow->SetLights({lamp});
    Check(glm::length(sample("board_in_point_shadow", draw(*shadow), 48, 48)) < .001f, "Blocked point light shone through the shade");

    // 白墙把点光源的光反射到绿板上。将这部分光烘焙后，再比较镜中有无这层反光。
    GIModel sideWall;
    sideWall.Transform = glm::scale(glm::translate(view, {1.2f, 0, .2f}), {.1f, 3.f, 3.f});
    Scope<GlobalIllumination> probes = world({mirror, board, sideWall, sign});
    probes->SetLights({lamp});
    effects.GetSettings().ReflectProbeLight = true;
    glm::vec3 fresh = sample("uncomputed_probes", draw(*probes), 48, 48);
    Check(fresh == lit, "New uncomputed probes affected the reflection");
    probes->BakeProbes(out / "mirror.rfprobes");
    glm::vec3 baked = sample("board_with_baked_bounce", draw(*probes), 48, 48);
    Check(baked.g > fresh.g + .001f, "The reflected board did not receive the baked light");
    effects.GetSettings().ReflectProbeLight = false;
    Check(sample("baked_bounce_off", draw(*probes), 48, 48) == fresh, "Disabled probes still lit the reflection");
    effects.GetSettings().ReflectProbeLight = true;
    probes->SetLights({});
    Check(glm::length(sample("light_removed", draw(*probes), 48, 48)) < .001f, "Reflection reused probes after the light changed");
    effects.GetSettings().ReflectProbeLight = false;

    // 增大粗糙度，原来整齐的白条会向两旁散开。取 16 次不同方向的平均色，减少单条随机光线对比较的影响。
    mirror.MaterialFactors = {{.55f, 1}};
    Scope<GlobalIllumination> roughMirror = world({mirror, sign});
    Image average(black);
    for (uint32_t frame = 0; frame < 16; ++frame)
    {
        Image next = draw(*roughMirror, frame);
        for (size_t i = 0; i < next.size(); ++i) average[i] += next[i] / 16.f;
    }
    glm::vec3 roughWhite = sample("rough_white_stripe_16_frames", average, 48, 31);
    Check(roughWhite.r < bright.r * .8f, "Rough mirror kept the narrow white stripe as bright as the polished mirror");
    Check(sample("beside_rough_stripe", average, 84, 31).r > .01f && local[31 * size + 84].r == 0.f,
          "Rough mirror darkened the stripe without spreading its light outside the polished silhouette");
    Save(out / "mirror-rough.png", average, size, size);

    // 斜放镜子后，灯牌原先就在镜子右边。向左平移相机，依次检查整块灯牌、半块灯牌和灯牌完全出画时的同一个镜面点。
    mirror.Transform = glm::scale(glm::rotate(glm::translate(view, {-.5f, 0, -3.f}),
                                    glm::radians(60.f), {0,1,0}), {.8f, 1.2f, .02f});
    mirror.MaterialFactors = {{0, 1}};
    GIModel solidLamp;
    solidLamp.Transform = glm::scale(glm::translate(view, {.55f, 0, -3.6f}), {.50f, 1.f, .02f});
    solidLamp.MaterialEmissions = {{2, .5f, .125f}};
    Scope<GlobalIllumination> moving = world({mirror, solidLamp});
    glm::vec3 firstPosition, firstColor;
    const char* names[] = {"pan_lamp_visible", "pan_lamp_at_edge", "pan_lamp_outside"};
    for (int step = 0; step < 3; ++step)
    {
        glm::vec3 camera(-.5f * step, 0, 0);
        glm::mat4 movedView = glm::translate(view, -camera);
        moving->SetCamera(projection * movedView, camera);
        device.Readback(moving->GetSurfaces(), visible.data(), visible.size() * sizeof(visible[0]));
        Image direct(black);
        int lampPixels = 0;
        for (size_t i = 0; i < visible.size(); ++i)
            if (visible[i].Position.w > 0 && std::abs(visible[i].Position.z + 3.59f) < .0001f)
            {
                direct[i] = glm::vec4(solidLamp.MaterialEmissions[0], 1); // 灯牌整面发出相同的光。
                ++lampPixels;
            }
        Check((step < 2) == (lampPixels > 0), "Camera path did not carry the lamp outside the view");
        device.Upload(input, direct.data(), direct.size() * sizeof(direct[0]));
        int x = 24 + 24 * step, y = 48;
        glm::vec3 position = visible[y * size + x].Position;
        if (step == 0) firstPosition = position;
        Check(glm::length(firstPosition - position) < .0001f, "Camera pan sampled a different point on the mirror");
        for (bool traceScene : {false, true})
        {
            effects.GetSettings().TraceScene = traceScene;
            Ref<ComputeResource> result = effects.Render(ScreenEffect::MaterialReflections, *moving, projection, movedView, 0, input);
            Image image(black.size());
            device.Readback(result, image.data(), image.size() * sizeof(image[0]));
            std::string label = std::string(names[step]) + (traceScene ? "_scene" : "_screen");
            glm::vec3 color = sample(label.c_str(), image, x, y);
            if (!traceScene)
            {
                if (step == 0) Check(color.r > 1.8f, "Initial camera did not reflect the lamp on screen");
                if (step == 2) Check(color.r == 0.f, "Screen-only mode found the lamp outside the camera");
            }
            else
            {
                if (step == 0) firstColor = color;
                Check(color.r > 1.8f && glm::length(color - firstColor) < .04f, "Reflected lamp changed as it left the screen");
            }
        }
    }
}
