#pragma once

#include "Redflag/Scene/SceneSerializer.h"

static void CheckSkeletonScene(VulkanContext& context, VulkanDraw& draw, const fs::path& work, std::ostream& report)
{
    Ref<Scene> scene = CreateRef<Scene>();
    scene->OnViewportResize(ViewportSize, ViewportSize);
    Check(SceneSerializer(scene).Deserialize("RedflagForge/assets/scenes/Character-Walk.redflag"), "Cannot open walking scene");
    Entity actor = scene->GetEntityByUUID(102);
    SkeletonAnimationComponent& player = actor.GetComponent<SkeletonAnimationComponent>();
    Check(player.Clip && player.Playing && !player.Bones.empty(), "Walk motion was not loaded");
    const glm::vec3 eye(2.2f, 1.15f, 2.3f);
    const glm::mat4 view = glm::lookAt(eye, glm::vec3(0,.9f,0), glm::vec3(0,1,0));
    const glm::mat4 projection = glm::perspective(.64f, 1.f, .05f, 25.f);
    const fs::path saved = work / "walk.redflag", probes = fs::path(saved).concat(".rfprobes");
    std::ofstream(fs::path(saved).concat(".render.yaml")) << "Shading: Lit\nIndirect: DDGI\nGTAO: false\nTAA: false\n";
    LightingPreview preview(context, draw.GetSampler());
    preview.Load(saved);
    preview.BakeProbes(*scene);
    const std::string room = Read(probes);

    // 亮面是否真正重画由正式视口的性能埋点判断。另读整张实体编号图，比较前后两帧的骨架轮廓。
    auto image = [&](bool expectNewSurfaces) {
        const fs::path trace = work / "skeleton-frame.json";
        RF_PROFILE_BEGIN_SESSION("skeleton", trace.string());
        Check(preview.Render(*scene, projection, view, eye, ViewportSize, ViewportSize, false) != VK_NULL_HANDLE, "Missing lit image");
        RF_PROFILE_END_SESSION();
        Check((Read(trace).find("Rasterize lighting surfaces") != std::string::npos) == expectNewSurfaces, "Unexpected lit surface update");
        Check(PanelText(preview, work / "skeleton-panel.txt").find("已烘焙") != std::string::npos && Read(probes) == room,
              "Dynamic bones invalidated the room bake");
        Check(context.BeginFrame(), "Could not render the character");
        draw.BeginScene();
        SceneRenderer::DrawGeometry(*scene, draw, projection * view);
        draw.EndScene();
        context.SwapBuffers();
        std::vector<bool> mask;
        for (int y = 0; y < ViewportSize; ++y)
            for (int x = 0; x < ViewportSize; ++x)
                mask.push_back(draw.ReadPixel(x,y).EntityID == int(static_cast<entt::entity>(actor)));
        return mask;
    };
    const std::vector<bool> first = image(true);
    scene->OnUpdateAnimations(.23f);
    const std::vector<bool> next = image(true);
    const int visible = int(std::count(next.begin(), next.end(), true));
    int changed = 0;
    for (size_t i = 0; i < next.size(); ++i) changed += next[i] != first[i];
    Check(visible > 0 && changed > 0, "GPU picking silhouette did not move");
    player.Playing = false;
    scene->OnUpdateAnimations(.5f);
    Check(image(false) == next, "Paused actor moved");
    SceneSerializer(scene).Serialize(saved.u8string());

    Ref<Scene> reopened = CreateRef<Scene>();
    reopened->OnViewportResize(ViewportSize, ViewportSize);
    Check(SceneSerializer(reopened).Deserialize(saved.u8string()), "Could not reopen saved animation");
    const auto& restored = reopened->GetEntityByUUID(102).GetComponent<SkeletonAnimationComponent>();
    Check(restored.Clip && restored.File == player.File && !restored.Playing && restored.Time == player.Time &&
          restored.Bones == player.Bones, "BVH path or paused pose changed on reopening");
    Ref<Scene> copy = Scene::Copy(reopened);
    auto& other = copy->GetEntityByUUID(102).GetComponent<SkeletonAnimationComponent>();
    other.Playing = true;
    copy->OnUpdateAnimations(.25f);
    Check(other.Bones != restored.Bones && !restored.Playing, "Scene copy shared playback state");
    report << "scene,clip,bones,seconds,actor_pixels,changed_pixels,paused_roundtrip,independent_copy,retained_bake\n"
           << "Character-Walk,Walk.bvh," << player.Bones.size() << ',' << player.Time << ',' << visible
           << ',' << changed << ",pass,pass,pass\n";
    context.WaitIdle();
}
