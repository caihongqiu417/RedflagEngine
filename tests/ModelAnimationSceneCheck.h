#pragma once

#include "Redflag/Assets/AssetManager.h"

static void CheckModelAnimationScene(VulkanContext& context, VulkanDraw& draw, const fs::path& work, std::ostream& report)
{
    fs::current_path("RedflagForge");
    Ref<Scene> scene = CreateRef<Scene>();
    scene->OnViewportResize(ViewportSize, ViewportSize);
    Check(SceneSerializer(scene).Deserialize("assets/scenes/Character-Fox.redflag"), "Cannot open Fox scene");
    Entity fox = scene->GetEntityByUUID(102);
    ModelAnimationComponent& player = fox.GetComponent<ModelAnimationComponent>();
    player.Playing = false;
    scene->OnUpdateAnimations(0.f);
    const Ref<ModelAnimation>& animation = player.GetAnimation();
    Check(animation && animation->GetClips().size() == 3 && player.GetMeshes().size() == 1, "Fox clips or mesh missing");
    Check(animation->GetClips()[1].Name == "Walk" && player.Clip == "Walk", "Wrong Walk clip");
    const auto& renderer = fox.GetComponent<ModelRendererComponent>();
    Check(renderer.ModelAsset->GetSubMeshes()[0].Mat->BaseColorMap != nullptr, "Missing original Fox texture");

    // 独立从原 GLB 的关键帧、逆绑定矩阵和权重计算。坐标在模型空间，尚未乘场景中的 0.021 倍缩放。
    const auto first = animation->Sample(1, 0.f), next = animation->Sample(1, .2f);
    Check(first[0].Vertices.size() == 1728 && animation->GetIndices(0).size() == 1728, "Fox triangle count changed");
    Check(glm::distance(first[0].Vertices[0].Position, {2.29131f,31.78290f,-23.11431f}) < .001f &&
          glm::distance(next[0].Vertices[900].Position, {.24271f,52.55789f,-46.16113f}) < .001f, "Fox skin differs from asset reference");

    const glm::vec3 eye(3.3f,1.6f,1.2f);
    const glm::mat4 view = glm::lookAt(eye, glm::vec3(0,.8f,0), glm::vec3(0,1,0));
    const glm::mat4 projection = glm::perspective(.65f, 1.f, .05f, 25.f);
    LightingPreview preview(context, draw.GetSampler());
    preview.Load("assets/scenes/Character-Fox.redflag");
    auto silhouette = [&](bool redraw) {
        const fs::path trace = work / "fox.json";
        RF_PROFILE_BEGIN_SESSION("Fox", trace.string());
        Check(preview.Render(*scene, projection, view, eye, ViewportSize, ViewportSize, false) != VK_NULL_HANDLE, "Missing lit Fox");
        RF_PROFILE_END_SESSION();
        Check((Read(trace).find("Rasterize lighting surfaces") != std::string::npos) == redraw, "Unexpected Fox Lit redraw");
        Check(context.BeginFrame(), "Vulkan frame failed");
        draw.BeginScene();
        SceneRenderer::DrawGeometry(*scene, draw, projection * view);
        draw.EndScene();
        context.SwapBuffers();
        std::vector<bool> mask;
        for (int y = 0; y < ViewportSize; ++y)
            for (int x = 0; x < ViewportSize; ++x)
                mask.push_back(draw.ReadPixel(x,y).EntityID == int(static_cast<entt::entity>(fox)));
        return mask;
    };
    const std::vector<bool> rest = silhouette(true);
    player.Playing = true;
    scene->OnUpdateAnimations(.2f);
    const std::vector<bool> walk = silhouette(true);
    int visible = int(std::count(walk.begin(), walk.end(), true)), changed = 0;
    for (size_t i = 0; i < rest.size(); ++i) changed += rest[i] != walk[i];
    Check(visible > 0 && changed > 0, "Fox silhouette did not change while walking");
    player.Playing = false;
    scene->OnUpdateAnimations(2.f);
    Check(silhouette(false) == walk, "Paused Fox moved");

    const fs::path saved = work / "fox.redflag";
    SceneSerializer(scene).Serialize(saved.u8string());
    Ref<Scene> reopened = CreateRef<Scene>();
    reopened->OnViewportResize(ViewportSize, ViewportSize);
    Check(SceneSerializer(reopened).Deserialize(saved.u8string()), "Cannot reopen Fox scene");
    reopened->OnUpdateAnimations(1.f);
    auto& restored = reopened->GetEntityByUUID(102).GetComponent<ModelAnimationComponent>();
    Check(restored.Time == player.Time && !restored.Playing && restored.Clip == player.Clip &&
          restored.GetAnimation() == animation, "Paused pose did not survive saving");
    Ref<Scene> copy = Scene::Copy(reopened);
    auto& other = copy->GetEntityByUUID(102).GetComponent<ModelAnimationComponent>();
    other.Clip = "Run";
    other.Playing = true;
    copy->OnUpdateAnimations(.1f);
    Check(other.GetMeshes()[0] != restored.GetMeshes()[0] && restored.Clip == "Walk" &&
          restored.Time == .2f, "Copy changed original pose");

    report << "scene,clip,seconds,vertices,triangles,actor_pixels,changed_pixels,paused_roundtrip,independent_copy\n"
           << "Character-Fox,Walk,0.2,1728,576," << visible << ',' << changed << ",pass,pass\n";
    context.WaitIdle();
    AssetManager::ClearAll();
}
