#include "rfpch.h"
#include "Platform/OpenGL/OpenGLCommandList.h"
#include "Platform/OpenGL/OpenGLRenderDevice.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Platform/Vulkan/VulkanDraw.h"
#include "Platform/Vulkan/VulkanComputeDevice.h"
#include "Redflag/Renderer/Framebuffer.h"
#include "Redflag/Renderer/Mesh.h"
#include "RenderDocCapture.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <spdlog/sinks/ostream_sink.h>

using namespace Redflag;

namespace {
    // ---- 两个后端共用的网格、贴图和检查点 ----
    void Require(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    // 两个方形共用四种颜色的贴图。固定在像素中心取样，可以逐字节核对颜色。
    struct TestMeshes
    {
        Ref<MeshGeometry> Near, Far;
        Ref<SampledTexture> Texture;
    };

    TestMeshes MakeScene(RenderDevice& device)
    {
        std::vector<MeshVertex> vertices = {
            {{-.55f, -.48f, 0}, {0,0,1}, {}, {1,0,0}},
            {{ .55f, -.48f, 0}, {0,0,1}, {}, {1,0,0}},
            {{ .55f,  .48f, 0}, {0,0,1}, {}, {1,0,0}},
            {{-.55f,  .48f, 0}, {0,0,1}, {}, {1,0,0}}
        };
        std::vector<uint32_t> indices{0, 1, 2, 2, 3, 0};

        // 近面读取左下的蓝色，远面读取右上的绿色。绘制位置由各自的矩阵决定。
        TestMeshes scene;
        for (auto& vertex : vertices)
            vertex.TexCoord = {.25f, .75f};
        scene.Near = device.CreateMesh(vertices, indices);
        for (auto& vertex : vertices)
            vertex.TexCoord = {.75f, .25f};
        scene.Far = device.CreateMesh(vertices, indices);

        const uint8_t pixels[] = {
            211, 36, 45, 255,   31, 208, 67, 255,
            43, 82, 231, 255,   247, 179, 20, 255
        };
        scene.Texture = device.CreateTexture(2, 2, pixels);
        return scene;
    }

    void DrawScene(CommandList& commands, const TestMeshes& scene)
    {
        const glm::mat4 camera(1);
        commands.DrawIndexed(scene.Near, scene.Texture, camera,
            glm::translate(glm::mat4(1), {-.25f, .25f, -.5f}), glm::vec4(1), 17);
        commands.DrawIndexed(scene.Far, scene.Texture, camera,
            glm::translate(glm::mat4(1), {.25f, .25f, .5f}), glm::vec4(1), 42);
    }

    template <typename ReadPixel>
    void CheckPixels(const char* api, uint32_t width, uint32_t height, ReadPixel read)
    {
        const glm::u8vec4 blue{43, 82, 231, 255}, green{31, 208, 67, 255};
        auto check = [&](uint32_t x, uint32_t y, int expectedID, glm::u8vec4 color) {
            auto pixel = read(x, y);
            if (pixel.EntityID != expectedID || (expectedID != -1 && pixel.Color != color))
            {
                std::ostringstream error;
                error << api << " (" << x << "," << y << ") entity=" << pixel.EntityID
                    << " expected " << expectedID << ", RGB=" << +pixel.Color.r << ","
                    << +pixel.Color.g << "," << +pixel.Color.b;
                throw std::runtime_error(error.str());
            }
        };

        check(width / 4, height / 4, 17, blue);
        check(3 * width / 4, height / 4, 42, green);
        check(width / 2, height / 4, 17, blue); // 两者重叠，先画的蓝色近面仍在前面。
        check(width / 2, 3 * height / 4, -1, {});
        std::cout << api << " " << width << "x" << height << ": left=17 right=42 overlap=17 background=-1\n";
    }

    // 中心像素穿过裁剪掉的近面，应读到同一块远面的颜色、位置和编号。
    void CheckLightingSurfaces(VulkanContext& context, VulkanDraw& picking, const TestMeshes& meshes)
    {
        constexpr uint32_t width = 33, height = 17, center = (height / 2) * width + width / 2;
        VulkanDraw gbuffer(context, VulkanDraw::Output::LightingSurfaces);
        gbuffer.Resize(width, height);
        picking.Resize(width, height);
        VulkanComputeDevice device(context);
        static const uint32_t code[] =
#include "Platform/Vulkan/Shaders/PackSurfaces.comp.inc"
        ;
        auto pack = device.CreatePipeline(code, sizeof(code),
            {ComputeBinding::Image, ComputeBinding::Image, ComputeBinding::Buffer}, sizeof(glm::mat4));
        struct Surface { glm::vec4 Position, Normal, Albedo; };
        std::vector<Surface> pixels(width * height);
        auto data = device.CreateBuffer(pixels.size() * sizeof(Surface));
        const auto view = glm::lookAt(glm::vec3(0, 0, 3), glm::vec3(0), glm::vec3(0, 1, 0));
        const uint8_t materialPixels[] = {241, 30, 216, 255, 9, 128, 64, 255};
        auto materialMap = gbuffer.CreateTexture(2, 1, materialPixels);
        for (bool ortho : {false, true})
        {
            const auto projection = ortho ? glm::ortho(-2.f, 2.f, -1.f, 1.f, .1f, 10.f)
                : glm::perspective(glm::radians(45.f), float(width) / height, .1f, 10.f);
            const auto vp = projection * view;
            Ref<SampledTexture> uploaded;
            SurfaceMaterial material;
            material.Metallic = .625f;
            material.Roughness = .8f;
            if (!ortho)
                material.MetallicRoughnessMap = materialMap;
            auto draw = [&](CommandList& commands) {
                commands.DrawIndexed(meshes.Far, meshes.Texture, vp, glm::mat4(1), glm::vec4(1), 42, material);
                // 场景首次遇到模型时，会在几何绘制尚未录完时上传它的贴图。
                if (!uploaded) {
                    const uint32_t white = 0xffffffff;
                    uploaded = gbuffer.CreateTexture(1, 1, &white);
                }
                SurfaceMaterial mask;
                mask.AlphaCutoff = .5f;
                commands.DrawIndexed(meshes.Near, uploaded, vp, glm::translate(glm::mat4(1), {0,0,.75f}),
                                     glm::vec4(1,1,1,.25f), 17, mask);
            };
            gbuffer.DrawSurfaces(draw);
            auto inverse = glm::inverse(vp);
            device.Dispatch(pack, {gbuffer.GetAlbedo(), gbuffer.GetNormalDepth(), data}, &inverse, 5, 3);
            device.Readback(data, pixels.data(), pixels.size() * sizeof(Surface));
            const auto& hit = pixels[center];
            Require(glm::length(hit.Position - glm::vec4(0,0,0,1)) < .001f, "Raster world position mismatch");
            Require(glm::length(glm::vec3(hit.Normal) - glm::vec3(0,0,1)) < .001f, "Raster world normal mismatch");
            // CPU 公式给出 0.630757。显卡的 sRGB 采样精度可不同，转回 8 位后应正好是原图的绿色 208。
            float srgb = 1.055f * std::pow(hit.Albedo.g, 1.f/2.4f) - .055f;
            Require(std::lround(255.f * srgb) == 208, "Raster base color is not linear RGB");
            const float metallic = material.Metallic * (ortho ? 1.f : 64.f / 255.f);
            const float roughness = material.Roughness * (ortho ? 1.f : 128.f / 255.f);
            Require(std::abs(hit.Albedo.w - metallic) < .00014f && std::abs(hit.Normal.w - roughness) < .00014f,
                    "Material factors or linear glTF B/G texture channels changed");
            Require(pixels[0].Position.w == 0, "Raster background contains a surface");
            Require(context.BeginFrame(), "Could not begin picking frame");
            picking.BeginScene();
            draw(picking);
            picking.EndScene();
            context.SwapBuffers();
            Require(picking.ReadPixel(width/2, height/2).EntityID == 42, "Picking ignores the alpha cutout");
            std::cout << (ortho ? "No material map" : "Linear glTF material map") << ": roughness "
                      << hit.Normal.w << ", metallic " << hit.Albedo.w << ", base green " << hit.Albedo.g << "\n";
        }

        // 同一张贴图的左半边全黑，右半边全白。模型外的清屏像素仍需和左半边区分。
        const uint32_t extremes[] = {0xff0000ff, 0xff0000ff, 0xffffff00, 0xffffff00};
        SurfaceMaterial material;
        material.Metallic = material.Roughness = 1.f;
        material.MetallicRoughnessMap = gbuffer.CreateTexture(4, 1, extremes);
        std::vector<MeshVertex> quad{
            {{-.8f,-.8f,0}, {0,0,1}, {0,0}, {1,0,0}},
            {{ .8f,-.8f,0}, {0,0,1}, {1,0}, {1,0,0}},
            {{ .8f, .8f,0}, {0,0,1}, {1,1}, {1,0,0}},
            {{-.8f, .8f,0}, {0,0,1}, {0,1}, {1,0,0}}
        };
        auto chart = gbuffer.CreateMesh(quad, {0,1,2,2,3,0});
        const glm::mat4 identity(1);
        gbuffer.DrawSurfaces([&](CommandList& draw) {
            draw.DrawIndexed(chart, {}, identity, identity, glm::vec4(1), 42, material);
        });
        std::vector<glm::vec4> colors(width * height);
        device.Readback(gbuffer.GetAlbedo(), colors.data(), colors.size() * sizeof(glm::vec4));
        device.Dispatch(pack, {gbuffer.GetAlbedo(), gbuffer.GetNormalDepth(), data}, &identity, 5, 3);
        device.Readback(data, pixels.data(), pixels.size() * sizeof(Surface));
        const auto left = center - width / 5, right = center + width / 5;
        Require(colors[0].a == 0 && colors[left].a == 1 && colors[right].a == 16777216.f,
                "Material encoding lost the sky or an endpoint");
        Require(pixels[0].Position.w == 0 && pixels[left].Position.w == 1 && pixels[right].Position.w == 1 &&
                pixels[left].Normal.w == 0 && pixels[left].Albedo.w == 0 &&
                pixels[right].Normal.w == 1 && pixels[right].Albedo.w == 1,
                "Material endpoints did not survive rasterization and unpacking");
        std::cout << "Lighting surfaces: sky/zero/one packed 0/1/16777216; projections, mask and picking passed\n";
    }

    void CheckEmission(VulkanContext& context)
    {
        constexpr int width = 96, height = 64;
        const glm::mat4 identity(1);
        VulkanComputeDevice device(context);
        VulkanDraw surfaces(context, VulkanDraw::Output::LightingSurfaces);
        VulkanDraw light(context, VulkanDraw::Output::Emission);
        surfaces.Resize(width, height);
        light.UseSceneDepth(surfaces);
        std::vector<MeshVertex> corners{
            {{-1,-1,0}, {0,0,1}, {0,0}}, {{1,-1,0}, {0,0,1}, {1,0}},
            {{1,1,0}, {0,0,1}, {1,1}}, {{-1,1,0}, {0,0,1}, {0,1}}
        };
        auto quad = light.CreateMesh(corners, {0,1,2,2,3,0});
        SurfaceMaterial panel;
        panel.Emission = {4,.48f,.14f};
        std::vector<uint8_t> texels;
        for (int i = 0; i < 12; ++i)
        {
            const uint8_t gray = i < 4 ? 0 : i < 8 ? 128 : 255;
            texels.insert(texels.end(), {gray,gray,gray,255});
        }
        panel.EmissiveMap = light.CreateTexture(12, 1, texels.data());
        auto reflected = std::vector<glm::vec4>(width * height, glm::vec4(.125f,.25f,.5f,1));
        auto source = device.CreateBuffer(reflected.size() * sizeof(glm::vec4), reflected.data());
        auto result = device.CreateBuffer(reflected.size() * sizeof(glm::vec4));
        static const uint32_t code[] =
#include "../RedflagForge/src/Shaders/AddEmission.comp.inc"
        ;
        auto add = device.CreatePipeline(code, sizeof(code),
            {ComputeBinding::Buffer, ComputeBinding::Image, ComputeBinding::Buffer, ComputeBinding::Image}, 0);
        auto render = [&](const glm::mat4& pose, bool occluder, bool cutout, bool coplanar = false, bool firstIsLight = false) {
            auto draw = [&](CommandList& cmd) {
                if (coplanar)
                {
                    SurfaceMaterial front;
                    if (firstIsLight) front.Emission = {0,2,0};
                    cmd.DrawIndexed(quad, {}, identity, pose, glm::vec4(1), 3, front);
                }
                cmd.DrawIndexed(quad, {}, identity, pose, glm::vec4(1), 1, panel);
                if (occluder)
                {
                    SurfaceMaterial screen;
                    if (cutout) screen.AlphaCutoff = .5f;
                    cmd.DrawIndexed(quad, {}, identity, glm::translate(identity, {0,0,-.4f}),
                                    glm::vec4(.8f,.8f,.8f,.25f), 2, screen);
                }
            };
            surfaces.DrawSurfaces(draw);
            Require(surfaces.HasEmission(), "Opaque lighting pass did not detect emissive material");
            light.DrawSurfaces(draw);
            device.Dispatch(add, {source, light.GetEmission(), result, surfaces.GetAlbedo()}, nullptr, width / 8, height / 8);
            std::vector<glm::vec4> pixels(reflected.size());
            device.Readback(result, pixels.data(), pixels.size() * sizeof(glm::vec4));
            return pixels;
        };
        const int black = width * (height/2) + width/6, gray = black + width/3, white = gray + width/3;
        auto visible = render(identity, false, false);
        auto linear = [&](size_t i) { return glm::vec3(visible[i] - reflected[i]); };
        Require(linear(black) == glm::vec3(0) && glm::length(linear(white) - panel.Emission) < .00001f,
                "Black and white emissive bands or HDR factor changed");
        Require(glm::length(linear(gray) - .21586f * panel.Emission) < .009f,
                "Emission map was not sampled in linear light");
        Require(render(identity, true, false) == reflected && render(identity, true, true) == visible,
                "Occluder or cutout did not agree with the lighting depth");
        Require(render(identity, false, false, true) == reflected, "Emission leaked through a coplanar solid wall");
        auto decals = render(identity, false, false, true, true);
        Require(glm::vec3(decals[white] - reflected[white]) == glm::vec3(0,2,0),
                "Two coplanar lights changed which material won the depth test");
        auto back = glm::scale(identity, {-1, 1, 1});
        Require(render(back, false, false) == reflected, "Single-sided sign glows on the back");
        panel.DoubleSided = true;
        Require(glm::length(glm::vec3(render(back, false, false)[black] - reflected[black]) - panel.Emission) < .00001f,
                "Two-sided sign did not emit from its back");
        for (auto& v : corners) v.EmissiveUVOffset = {1.f/3, 0};
        quad = light.CreateMesh(corners, {0,1,2,2,3,0});
        auto shifted = render(identity, false, false);
        Require(shifted[black] == visible[gray] && shifted[gray] == visible[white] && shifted[white] == visible[black],
                "Emissive map did not use its own UV coordinates");
        surfaces.DrawSurfaces([&](CommandList& cmd) { cmd.DrawIndexed(quad, {}, identity, identity, glm::vec4(1), 1); });
        Require(!surfaces.HasEmission(), "Non-emissive scene requests another surface pass");
        std::cout << "Emission linear RGB (sRGB bytes 0/128/255): ";
        for (auto i : {black,gray,white})
        {
            auto rgb = linear(i);
            std::cout << "(" << rgb.r << "," << rgb.g << "," << rgb.b << ") ";
        }
        std::cout << "occlusion, cutout, coplanar depth, front/back and emission UV passed\n";
    }

    void CheckTransparency(VulkanContext& context, const TestMeshes& meshes)
    {
        constexpr uint32_t width = 192, height = 112;
        VulkanComputeDevice device(context);
        VulkanDraw opaque(context, VulkanDraw::Output::LightingSurfaces);
        VulkanDraw transparent(context, VulkanDraw::Output::Transparency);
        opaque.Resize(width, height);
        transparent.UseSceneDepth(opaque);
        opaque.Resize(width / 2, height / 2);
        opaque.Resize(width, height);
        Require(transparent.UseSceneDepth(opaque), "Glass kept a retired depth image of the same size");

        auto vp = glm::perspective(glm::radians(45.f), float(width) / height, .1f, 10.f) *
            glm::lookAt(glm::vec3(0,0,3), glm::vec3(0), glm::vec3(0,1,0));
        const auto tilt = glm::rotate(glm::mat4(1), glm::radians(38.f), glm::vec3(0,1,0));
        std::vector<glm::vec4> background(width * height);
        for (uint32_t i = 0; i < background.size(); ++i)
            background[i] = (i % width / 4) % 2 ? glm::vec4(.9,.8,.65,1) : glm::vec4(.035,.09,.2,1);
        auto source = device.CreateBuffer(background.size() * sizeof(glm::vec4), background.data());
        auto output = device.CreateBuffer(background.size() * sizeof(glm::vec4));
        static const uint32_t code[] =
#include "Platform/Vulkan/Shaders/CompositeTransparency.comp.inc"
        ;
        auto composite = device.CreatePipeline(code, sizeof(code), {ComputeBinding::Buffer, ComputeBinding::Image,
            ComputeBinding::Image, ComputeBinding::Image, ComputeBinding::Image, ComputeBinding::Buffer},
            2 * sizeof(glm::mat4));
        auto run = [&](float ior, bool reversed, bool blocked, bool back = false, bool doubleSided = false, bool emit = false) {
            auto draw = [&](CommandList& commands) {
                if (blocked) commands.DrawIndexed(meshes.Near, {}, vp, glm::translate(glm::mat4(1), {0,0,1}),
                                                   glm::vec4(1), 7);
                for (int i = 0; i < 2; ++i)
                {
                    const bool front = reversed ? i == 0 : i == 1;
                    auto pose = glm::translate(glm::mat4(1), {front ? .1f : -.1f, 0, front ? .4f : 0}) * tilt;
                    if (back) pose = glm::rotate(pose, glm::radians(180.f), glm::vec3(0,1,0));
                    SurfaceMaterial glass;
                    glass.Blend = true;
                    glass.DoubleSided = doubleSided;
                    if (emit) glass.Emission = {2,0,0};
                    glass.Thickness = .4f;
                    glass.RefractionIndex = ior;
                    glass.Metallic = .8f;
                    glass.Roughness = .3f;
                    auto tint = front ? glm::vec4(.65,.9,1,.22) : glm::vec4(1,.6,.4,.35);
                    commands.DrawIndexed(meshes.Near, {}, vp, pose, tint, front ? 2 : 3, glass);
                }
            };
            opaque.DrawSurfaces(draw);
            Require(opaque.HasTransparency(), "Transparent draw was not detected");
            transparent.DrawSurfaces(draw);
            glm::mat4 camera[] = {vp, glm::inverse(vp)};
            device.Dispatch(composite, {source, transparent.GetAlbedo(), transparent.GetNormalDepth(),
                transparent.GetOptics(), opaque.GetNormalDepth(), output}, camera, width/8, height/8);
            std::vector<glm::vec4> pixels(width * height);
            device.Readback(output, pixels.data(), pixels.size() * sizeof(glm::vec4));
            return pixels;
        };
        auto straight = run(1.f, false, false), bent = run(1.5f, false, false);
        auto reverse = run(1.5f, true, false), blocked = run(1.5f, false, true);
        auto back = run(1.5f, false, false, true), both = run(1.5f, false, false, true, true);
        float orderError = 0, bendError = 0;
        size_t bentPixels = 0;
        for (size_t i = 0; i < bent.size(); ++i)
        {
            orderError = std::max(orderError, glm::length(bent[i] - reverse[i]));
            float error = glm::length(bent[i] - straight[i]);
            bendError = std::max(bendError, error);
            bentPixels += error > .02f;
        }
        Require(orderError < .00001f && bentPixels > 100, "Transparent order or refraction mismatch");
        const size_t center = width * (height/2) + width/2;
        auto glowing = run(1.f, false, false, false, false, true);
        float emittedRed = glowing[center].r - straight[center].r;
        Require(std::abs(emittedRed - 2.f * (1.f - .78f * .65f)) < .00002f,
                "Transparent emission does not follow layer opacity");
        std::cout << "Two glowing glass layers (.22, .35): added red " << emittedRed << "\n";
        Require(back == background && glm::length(both[center] - background[center]) > .02f,
                "Single-sided and double-sided glass do not follow the mesh winding");
        Require(glm::length(blocked[center]-background[center]) < .00001f, "Glass leaks through opaque geometry");
        Require(glm::length(bent[0]-background[0]) == 0.f, "Glass changes uncovered background");
        std::cout << "Transparency: order max " << orderError << ", refraction changed " << bentPixels << "/"
            << bent.size() << " pixels above 0.02 linear RGB distance, maximum " << bendError << "\n";
        if (const char* path = std::getenv("RF_GLASS_IMAGES"))
        {
            for (const auto& shot : {std::make_pair("straight", &straight), std::make_pair("bent", &bent)})
            {
                std::ofstream file(std::string(path) + "/" + shot.first + ".rgba32f", std::ios::binary);
                file.write(reinterpret_cast<const char*>(shot.second->data()), shot.second->size() * sizeof(glm::vec4));
            }
        }
    }

    // ---- Vulkan 窗口、离屏绘制与读回 ----
    void RunVulkan(RenderDocCapture& capture)
    {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        auto window = std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)>(
            glfwCreateWindow(128, 96, "RHI Vulkan", nullptr, nullptr), glfwDestroyWindow);
        Require(window != nullptr, "Vulkan test window creation failed");

        VulkanContext context(window.get());
        context.Init();
        VulkanDraw renderer(context);
        TestMeshes scene = MakeScene(renderer);
        CheckLightingSurfaces(context, renderer, scene);
        CheckEmission(context);
        CheckTransparency(context, scene);
        const uint32_t firstGeneration = context.GetSwapchainGeneration();

        for (auto size : {32u, 64u})
        {
            const uint32_t height = size / 2;
            Require(renderer.Resize(size, height), "Vulkan target was not resized");
            Require(!renderer.Resize(size, height), "Vulkan recreated a target of the same size");
            if (size == 64)
                glfwSetWindowSize(window.get(), 160, 120);

            // 连画三帧，第三帧会再次用到第一帧的命令池。
            for (int i = 0; i < 3; ++i)
            {
                glfwPollEvents();
                const bool captureFrame = size == 32 && i == 1;
                if (captureFrame) capture.Begin();
                Require(context.BeginFrame(), "Could not start Vulkan test frame");

                renderer.BeginScene();
                DrawScene(renderer, scene);
                renderer.EndScene();

                if (size == 64 && i == 2)
                    scene = {}; // 已录制的命令仍需要目标网格和贴图。
                context.SwapBuffers();
                if (captureFrame) capture.End();
            }

            CheckPixels("Vulkan", size, height, [&](uint32_t x, uint32_t y) { return renderer.ReadPixel(x, y); });
        }
        Require(context.GetSwapchainGeneration() > firstGeneration, "Window swapchain did not resize");
    }

    // ---- OpenGL 用相同的输入与像素坐标对照 ----
    void RunOpenGL()
    {
        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        auto window = std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)>(
            glfwCreateWindow(128, 96, "RHI OpenGL", nullptr, nullptr), glfwDestroyWindow);
        Require(window != nullptr, "OpenGL test window creation failed");
        glfwMakeContextCurrent(window.get());
        Require(gladLoadGLLoader((GLADloadproc)glfwGetProcAddress) != 0, "OpenGL driver unavailable");

        OpenGLRenderDevice device;
        OpenGLCommandList commands;
        TestMeshes scene = MakeScene(device);

        FramebufferSpecification spec;
        spec.Width = 32;
        spec.Height = 16;
        spec.Attachments = {FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::RED_INTEGER,
                            FramebufferTextureFormat::Depth};
        auto target = Framebuffer::Create(spec);
        glEnable(GL_DEPTH_TEST);

        for (auto size : {32u, 64u})
        {
            const uint32_t height = size / 2;
            if (size != spec.Width) target->Resize(size, height);

            for (int i = 0; i < 3; ++i)
            {
                target->Bind();
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                target->ClearAttachment(1, -1);
                DrawScene(commands, scene);
            }

            // 约定的检查点从左上起算，OpenGL 读取颜色和编号时从最下面一行起算。
            CheckPixels("OpenGL", size, height, [&](uint32_t x, uint32_t y) {
                int row = static_cast<int>(height - 1 - y);
                VulkanDraw::Pixel pixel{};
                pixel.EntityID = target->ReadPixel(1, x, row);
                glReadBuffer(GL_COLOR_ATTACHMENT0);
                glReadPixels(x, row, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, &pixel.Color);
                return pixel;
            });

            target->Unbind();
            Require(glGetError() == GL_NO_ERROR, "OpenGL reported an error");
        }
    }
}

int main(int argc, char** argv)
{
    if (argc != 1 && !(argc == 3 && std::string(argv[1]) == "--capture"))
        return 1;

    RenderDocCapture capture(argc == 3 ? argv[2] : nullptr, "../output/captures/RHI",
        "Redflag Vulkan indexed mesh and depth", "32x16 offscreen target. Near blue entity 17, far green entity 42.");
    Log::Init();
    std::ostringstream validationErrors;
    auto errors = std::make_shared<spdlog::sinks::ostream_sink_mt>(validationErrors);
    errors->set_level(spdlog::level::err);
    Log::GetCoreLogger()->sinks().push_back(errors);

    if (!glfwInit()) return 1;
    try
    {
        RunVulkan(capture);
        if (!validationErrors.str().empty())
            throw std::runtime_error(validationErrors.str());

        RunOpenGL();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        glfwTerminate();
        return 1;
    }

    glfwTerminate();
    std::cout << "RHI pixel and resize checks passed\n";
    return 0;
}
