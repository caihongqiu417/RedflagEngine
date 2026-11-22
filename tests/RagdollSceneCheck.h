#pragma once

#include "Redflag/Scene/SceneSerializer.h"

static void CheckRagdollScene(VulkanContext& context, VulkanDraw& draw, const fs::path& work, std::ostream& report)
{
    const fs::path sample = "RedflagForge/assets/scenes/Character-Physics.redflag";
    Ref<Scene> edit = CreateRef<Scene>();
    edit->OnViewportResize(ViewportSize, ViewportSize);
    Check(SceneSerializer(edit).Deserialize(sample.u8string()), "Cannot open the character physics scene");
    Entity original = edit->GetEntityByUUID(102);
    glm::vec3 start = original.GetComponent<TransformComponent>().Translation;
    Check(!edit->GetRagdoll(original), "Edit mode created a physics body");
    const fs::path saved = work / "character.redflag";
    RagdollComponent& settings = original.GetComponent<RagdollComponent>();
    settings.Walking = false;
    settings.Speed = -.45f;
    SceneSerializer(edit).Serialize(saved.u8string());
    Ref<Scene> reopened = CreateRef<Scene>();
    reopened->OnViewportResize(ViewportSize, ViewportSize);
    Check(SceneSerializer(reopened).Deserialize(saved.u8string()), "Cannot reopen character physics settings");
    const RagdollComponent& read = reopened->GetEntityByUUID(102).GetComponent<RagdollComponent>();
    Check(!read.Walking && read.Speed == -.45f, "Walk switch or signed speed lost in YAML");
    settings.Walking = true;
    settings.Speed = .8f;

    Ref<Scene> scene = Scene::Copy(edit);
    scene->OnSimulationStart();
    Entity actor = scene->GetEntityByUUID(102);
    TransformComponent& root = actor.GetComponent<TransformComponent>();
    const auto hit = scene->Raycast(start + glm::vec3(0, 2, 0), {0,-1,0}, 3);
    Check(hit && hit->Entity == 102, "Pelvis picking returned an internal limb ID");
    const auto nearby = scene->OverlapSphere(start, .7f);
    Check(nearby == std::vector<uint64_t>{102}, "The character should appear once when several limbs overlap");

    const glm::vec3 eye(2.8f, 1.6f, 4);
    const glm::mat4 view = glm::lookAt(eye, glm::vec3(0,.55f,0), glm::vec3(0,1,0));
    const glm::mat4 projection = glm::perspective(.68f, 1.f, .05f, 40.f);
    LightingPreview preview(context, draw.GetSampler());
    preview.Load(sample);
    auto image = [&]() {
        fs::path trace = work / "surfaces.json";
        RF_PROFILE_BEGIN_SESSION("physics character", trace.string());
        VkDescriptorSet color = preview.Render(*scene, projection, view, eye, ViewportSize, ViewportSize, false);
        RF_PROFILE_END_SESSION();
        Check(color && Read(trace).find("Rasterize lighting surfaces") != std::string::npos, "Lit view did not repaint the new pose");
        Check(context.BeginFrame(), "Cannot render the character picking image");
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

    auto advance = [&](int frames, int hz) {
        for (int i = 0; i < frames; ++i) scene->OnUpdatePhysics(1.f / hz);
    };
    const std::vector<bool> standing = image();
    advance(180,60);
    const glm::vec3 walked = root.Translation;
    Check(walked.x > start.x + 1.f && std::abs(walked.z - start.z) < .4f && walked.y > .7f,
          "The saved character did not walk upright along its initial heading");
    const std::vector<bool> walking = image();
    int changed = 0;
    for (size_t i = 0; i < walking.size(); ++i) changed += walking[i] != standing[i];
    Check(changed && std::count(walking.begin(), walking.end(), true) && std::count(standing.begin(), standing.end(), true),
          "Character was absent or stationary on the Vulkan picking image");

    // 每秒变换送来 30、60、120 帧，让物理关节都使用相同的 180 个固定步。
    float maxDifference = 0;
    for (int hz : {30, 120})
    {
        Ref<Scene> other = Scene::Copy(edit);
        other->OnSimulationStart();
        for (int frame = 0; frame < 3*hz; ++frame) other->OnUpdatePhysics(1.f / hz);
        glm::vec3 position = other->GetEntityByUUID(102).GetComponent<TransformComponent>().Translation;
        maxDifference = std::max(maxDifference, glm::distance(position, walked));
        other->OnSimulationStop();
    }
    Check(maxDifference < .02f, "Walking changed with the rendering frame interval");

    actor.GetComponent<RagdollComponent>().Speed = 0;
    advance(120,60);
    float stopDistance = glm::distance(root.Translation, walked);
    Check(stopDistance < .5f && root.Translation.y > .7f, "Zero speed did not keep the character standing");
    actor.GetComponent<RagdollComponent>().Walking = false;
    advance(180,60);
    float relaxedHeight = root.Translation.y;
    Check(relaxedHeight > 0 && relaxedHeight < .65f, "The released pelvis did not drop to the ground");
    Check(image() != walking, "The viewport did not show the collapsed pose");

    // 在模拟中复制角色，抬高新角色，再移除原角色。两副关节应当各管各的身体。
    scene->DuplicateEntity(actor);
    Entity copy;
    for (entt::entity handle : scene->GetRegistry().view<RagdollComponent>())
        if (handle != static_cast<entt::entity>(actor)) copy = Entity(handle,scene.get());
    Check(bool(copy), "Duplicating the entity lost its physics character");
    TransformComponent& otherRoot = copy.GetComponent<TransformComponent>();
    otherRoot.Translation = {-3,2,0};
    otherRoot.Rotation = glm::vec3(0);
    advance(1,60);
    const auto above = scene->Raycast({-3,3,0}, {0,-1,0}, 2);
    Check(above && above->Entity == uint64_t(copy.GetUUID()), "The duplicate was not independently pickable");
    actor.RemoveComponent<RagdollComponent>();
    Check(!scene->GetRagdoll(actor) && scene->GetRagdoll(copy), "Removing one component destroyed the wrong character");
    advance(180,60);
    float dropped = otherRoot.Translation.y;
    Check(dropped > 0 && dropped < .7f, "The passive duplicate did not land on the floor");
    scene->DestroyEntity(copy);
    const auto removed = scene->Raycast({-3,3,0}, {0,-1,0}, 4);
    Check(removed && removed->Entity == 103, "Deleted limbs still cover the floor");
    scene->OnSimulationStop();
    Check(!scene->Raycast(start, {0,-1,0}, 4) && original.GetComponent<TransformComponent>().Translation == start,
          "Stopping simulation left physical actors or moved the saved character");

    // 游戏启动时也要造出独立的角色。脚本系统按游戏工作目录加载程序集。
    fs::path previous = fs::current_path();
    fs::current_path("RedflagForge");
    ScriptEngine::Init();
    Ref<Scene> game = Scene::Copy(edit);
    game->OnRuntimeStart();
    Entity gameActor = game->GetEntityByUUID(102);
    for (int frame = 0; frame < 180; ++frame) game->OnUpdatePhysics(1.f/60.f);
    const glm::vec3 gamePose = gameActor.GetComponent<TransformComponent>().Translation;
    game->OnRuntimeStop();
    ScriptEngine::Shutdown();
    fs::current_path(previous);
    Check(glm::distance(gamePose, walked) < .02f && !game->GetRagdoll(gameActor), "Game start or stop differs from simulation");
    report << "scene,seconds,speed_m_s,forward_m,sideways_m,pelvis_m,zero_speed_drift_m,relaxed_pelvis_m,dropped_pelvis_m,"
              "max_position_difference_at_30_60_120_hz_m,viewport_changed_pixels,save_open,copy,remove,game_start_stop\n"
           << "Character-Physics,3,0.8," << walked.x-start.x << ',' << walked.z-start.z << ',' << walked.y << ','
           << stopDistance << ',' << relaxedHeight << ',' << dropped << ',' << maxDifference << ',' << changed
           << ",pass,pass,pass,pass\n";
    context.WaitIdle();
}
