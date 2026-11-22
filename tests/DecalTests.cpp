#include "rfpch.h"
#include "../RedflagForge/src/LightingPreview.h"
#include "../Redflag/vendor/stb_image/stb_image_write.h"
#include "Redflag/Assets/AssetManager.h"
#include "RenderDocCapture.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Platform/Vulkan/VulkanDraw.h"
#include "Redflag/Core/Application.h"
#include "Redflag/Renderer/DecalRenderer.h"
#include "Redflag/Renderer/RendererAPI.h"
#include "Redflag/Scene/Entity.h"
#include "Redflag/Scene/SceneSerializer.h"
#include <glm/gtc/matrix_transform.hpp>

using namespace Redflag;
namespace fs = std::filesystem;

static void Check(bool passed, const char* text)
{
    if (!passed) throw std::runtime_error(text);
    std::cout << "PASS " << text << '\n';
}

static void CheckPixels(VulkanContext& context, const fs::path& folder)
{
    // 前一行白色完全不透明，后一行的 alpha 为零，直接与 GPU 计算的覆盖率对照。
    const fs::path file = folder / "coverage.tga";
    const uint8_t header[18] = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 2, 0, 32, 0x28};
    const uint32_t colors[4] = {0xffffffff, 0xffffffff, 0x00ffffff, 0x00ffffff};
    {
        std::ofstream out(file, std::ios::binary);
        out.write((const char*)header, sizeof(header));
        out.write((const char*)colors, sizeof(colors));
    }

    VulkanComputeDevice device(context);
    DecalRenderer decals(device);
    Scene scene;
    Entity projection = scene.CreateEntityWithUUID(41, "测试贴花");
    DecalComponent& paint = projection.AddComponent<DecalComponent>();
    paint.Texture = file.u8string();
    paint.Tint = {1, 0, 0, 1};
    paint.Roughness = .82f;
    paint.Metallic = .62f;
    Check(decals.Update(scene) && !decals.Update(scene), "unchanged scene reuses the uploaded decal");
    struct Surface
    {
        glm::vec4 Position, Normal, Albedo;
    };
    std::vector<Surface> original;
    constexpr int side = 8;
    auto index = [side](int x, int y) { return y * side + x; };
    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x)
            original.push_back({{(x + .5f) / 4 - 1, 0, (y + .5f) / 4 - 1, 1}, {0, 1, 0, .35f}, {.2f, .3f, .4f, .1f}});
    original[index(3, 2)].Normal = glm::vec4(1, 0, 0, .35f);
    original[index(4, 2)].Position.y = .2f;
    original[index(5, 2)].Position.w = 0;
    original[index(2, 3)].Normal = glm::vec4(.8660254f, .5f, 0, .35f);
    const glm::mat4 vp =
        glm::ortho(-1.f, 1.f, -1.f, 1.f, .1f, 4.f) * glm::lookAt(glm::vec3(0, 2, 0), glm::vec3(0), glm::vec3(0, 0, -1));
    auto buffer = device.CreateBuffer(original.size() * sizeof(Surface), original.data());
    auto paintFrame = [&]() {
        device.Upload(buffer, original.data(), original.size() * sizeof(Surface));
        decals.Draw(buffer, vp, {side, side});
        std::vector<Surface> current(original.size());
        device.Readback(buffer, current.data(), current.size() * sizeof(Surface));
        return current;
    };
    const auto current = paintFrame();
    const auto& marked = current[index(2, 2)];
    Check(glm::length(marked.Albedo - glm::vec4(1, 0, 0, .62f)) < .0001f && std::abs(marked.Normal.w - .82f) < .0001f,
          "opaque decal updates linear albedo, roughness and metalness");
    Check(marked.Position == original[index(2, 2)].Position && glm::vec3(marked.Normal) == glm::vec3(0, 1, 0),
          "decal keeps position and normal");
    for (int i : {index(1, 2), index(3, 2), index(4, 2), index(5, 2), index(2, 5)})
        Check(std::memcmp(&current[i], &original[i], sizeof(Surface)) == 0,
              "outside box, grazing wall, depth, sky or transparent texel is unchanged");
    Check(current[index(2, 3)].Albedo.r > .2f && current[index(2, 3)].Albedo.r < .8f,
          "inclined surface receives a faded decal");

    Entity top = scene.CreateEntityWithUUID(42, "上层贴花");
    DecalComponent blue = paint;
    blue.Tint = {0, 0, 1, .5f};
    blue.Order = 2;
    top.AddComponent<DecalComponent>(blue);
    decals.Update(scene);
    Check(glm::length(glm::vec3(paintFrame()[index(2, 2)].Albedo) - glm::vec3(.5f, 0, .5f)) < .0001f,
          "ordered decals blend through alpha");
    scene.DestroyEntity(top);
    fs::path moved = folder / "coverage-moved.tga";
    fs::rename(file, moved);
    Check(decals.Update(scene), "missing image invalidates the cached projection");
    Check(paintFrame()[index(2, 2)].Albedo == original[index(2, 2)].Albedo, "missing image reveals the original surface");
    fs::rename(moved, file);
    Check(decals.Update(scene) && paintFrame()[index(2, 2)].Albedo == marked.Albedo, "returning the image restores the decal");
    scene.DestroyEntity(projection);
    Check(decals.Update(scene), "removing the last decal invalidates the view");
    const auto clear = paintFrame();
    Check(std::memcmp(clear.data(), original.data(), clear.size() * sizeof(Surface)) == 0,
          "clearing all decals restores the original surface");
}

static void CheckScene(VulkanContext& context, VulkanDraw& device, const fs::path& out, RenderDocCapture& capture)
{
    constexpr int w = 640, h = 416;
    Ref<Scene> scene = CreateRef<Scene>();
    scene->OnViewportResize(w, h);
    SceneSerializer reader(scene);
    const fs::path file = "assets/scenes/Decal-Paint.redflag";
    Check(reader.Deserialize(file.string()), "open saved decal sample scene");
    Entity left = scene->GetEntityByUUID(105), wall = scene->GetEntityByUUID(106);
    Check(left && wall && left.HasComponent<DecalComponent>() && wall.HasComponent<DecalComponent>(),
          "open two authored decal projectors");
    SceneSerializer(scene).Serialize((out / "resaved.redflag").string());
    Ref<Scene> reopened = CreateRef<Scene>();
    reopened->OnViewportResize(w, h);
    Check(SceneSerializer(reopened).Deserialize((out / "resaved.redflag").string()), "read serialized scene");
    const auto& saved = scene->GetEntityByUUID(105).GetComponent<DecalComponent>();
    const auto& loaded = reopened->GetEntityByUUID(105).GetComponent<DecalComponent>();
    Check(saved.Texture == loaded.Texture && saved.Size == loaded.Size && saved.Tint == loaded.Tint &&
              saved.MaxAngle == loaded.MaxAngle && saved.Order == loaded.Order &&
              saved.Roughness == loaded.Roughness && saved.Metallic == loaded.Metallic,
          "texture, projection size, tint and max angle survive save and reopen");

    Ref<Scene> game = Scene::Copy(scene);
    game->GetEntityByUUID(105).GetComponent<DecalComponent>().Tint.a = 0.f;
    Check(saved.Tint.a > 0.f, "play mode owns its decal settings");

    LightingPreview preview(context, device.GetSampler());
    preview.Load(file);
    Entity camera = scene->GetPrimaryCameraEntity();
    const auto& pose = camera.GetComponent<TransformComponent>();
    const glm::vec3 eye = pose.Translation;
    const glm::mat4 view = glm::inverse(pose.GetTransform());
    const glm::mat4 projection = camera.GetComponent<CameraComponent>().Camera.GetProjection();
    auto draw = [&](const char* name) {
        preview.Render(*scene, projection, view, eye, w, h, false, false);
        const auto pixels = preview.ReadDisplayPixels();
        Check(pixels.size() == w * h, "lit frame returned at requested size");
        Check(stbi_write_png((out / (std::string(name) + ".png")).u8string().c_str(), w, h, 4, pixels.data(), w * 4) != 0,
              "save rendered pixels");
        return pixels;
    };
    capture.Begin();
    const auto both = draw("paint");
    capture.End();
    scene->DestroyEntity(wall);
    const auto floor = draw("floor");
    scene->DestroyEntity(left);
    const auto plain = draw("plain");
    size_t wallChanged = 0, floorChanged = 0;
    for (size_t i = 0; i < plain.size(); ++i)
    {
        wallChanged += both[i] != floor[i];
        floorChanged += floor[i] != plain[i];
    }
    std::cout << "wall_pixels=" << wallChanged << " floor_pixels=" << floorChanged << '\n';
    Check(wallChanged > 30 && floorChanged > 120, "wall and floor projectors both change the actual Lit frame");
    LightingPreview fresh(context, device.GetSampler());
    fresh.Load(file);
    fresh.Render(*scene, projection, view, eye, w, h, false, false);
    Check(fresh.ReadDisplayPixels() == plain, "cleared frame matches a fresh rendering without decals");
}

int main(int argc, char** argv)
try
{
    if (argc < 3 || argc > 4) throw std::runtime_error("Use --decal OUTPUT [RenderDoc DLL]");
    fs::path out = fs::absolute(fs::u8path(argv[2]));
    fs::create_directories(out);
    RenderDocCapture capture(argc == 4 ? argv[3] : nullptr, out / "paint", "Surface decals: floor and wall",
                             "Vulkan Lit output at 640x416. Two projectors reuse the same PNG.");
    Log::Init();
    RendererAPI::SetAPI(RendererAPI::API::Vulkan);
    Application app("Decal validation", false);
    auto& context = static_cast<VulkanContext&>(app.GetWindow().GetGraphicsContext());
    VulkanDraw device(context);
    RenderDevice::SetCurrent(&device);
    CheckPixels(context, out);
    CheckScene(context, device, out, capture);
    context.WaitIdle();
    AssetManager::ClearAll();
    RenderDevice::SetCurrent(nullptr);
    return 0;
}
catch (const std::exception& e)
{
    std::cerr << e.what() << '\n';
    return 1;
}
