#include "rfpch.h"
#include "Scene.h"
#include "Entity.h"
#include "Redflag/Assets/AssetManager.h"
#include "Redflag/Animation/ActorAnimator.h"
#include "Redflag/Animation/MontagePlayer.h"
#include "Redflag/Gameplay/ColliderBounds.h"
#include "Redflag/Gameplay/HandContact.h"
#include "Redflag/Gameplay/TerrainTraversal.h"
#include "Redflag/Gameplay/TraversalRig.h"

namespace Redflag
{
    void Scene::OnUpdateAnimations(Timestep ts, std::optional<glm::vec2> movement)
    {
        for (entt::entity id : m_Registry.view<FoliageComponent>())
        {
            FoliageComponent& patch = m_Registry.get<FoliageComponent>(id);
            if (patch.Visible && patch.Wind) patch.Seconds += ts.GetSeconds();
        }
        if (movement && glm::length(*movement) > 1.f) *movement = glm::normalize(*movement);
        m_Registry.view<TransformComponent, SkeletonAnimationComponent>().each([&](TransformComponent& transform, SkeletonAnimationComponent& player) {
            if (movement && player.HasControl())
            {
                const glm::vec2 velocity = player.Movement.value_or(*movement) * player.MoveSpeed;
                const glm::vec3 world(velocity.x, 0.f, velocity.y);
                transform.Translation += world * ts.GetSeconds();

                // 方向键沿世界地面移动。实体若已绕 Y 轴旋转，骨架要把同一个方向换算到自己的坐标中。
                const glm::vec3 local = glm::angleAxis(-transform.Rotation.y, glm::vec3(0,1,0)) * world;
                player.Move(ts.GetSeconds(), {local.x, local.z});
            }
            else if (player.Playing && player.Clip)
                player.Seek(player.Time + ts.GetSeconds());
        });

        PrepareContacts(*this,ts.GetSeconds());
        m_Registry.view<ModelRendererComponent, ModelAnimationComponent>().each([&](entt::entity handle, ModelRendererComponent& renderer, ModelAnimationComponent& player) {
            if (!renderer.ModelAsset && renderer.ModelID) renderer.ModelAsset = AssetManager::Load<Model>(renderer.ModelID);
            Entity owner(handle,this);
            MontagePlayer* montage = m_Registry.try_get<MontagePlayer>(handle);
            const bool compose = montage && montage->HasPose();
            if (compose) {
                montage->Advance(ts.GetSeconds(), [&](const MontageTarget& target, const glm::vec3& clipStep,
                    const glm::vec3& clipRemaining, float portion, float seconds) {
                    TransformComponent& transform = owner.GetComponent<TransformComponent>();
                    const glm::mat3 modelToWorld(transform.GetTransform());
                    // 保留原片的移动曲线，将剩余的落点误差分摊到窗口里。每一步仍按角色胶囊检查地面和障碍。
                    const glm::vec3 correction = target.Position - transform.Translation - modelToWorld * clipRemaining;
                    glm::vec3 delta = modelToWorld * clipStep + portion * correction;
                    delta.y = 0.f;
                    MoveCharacter(*this,owner.GetUUID(),delta,CharacterRadius(owner),seconds);
                    transform.Rotation.y += portion * std::remainder(target.Yaw - transform.Rotation.y,glm::two_pi<float>());
                });
            }
            auto* rig = m_Registry.try_get<TraversalRig>(handle);
            const auto* motion = m_Registry.try_get<TerrainTraversal>(handle);
            auto* hand = m_Registry.try_get<HandContact>(handle);
            float timelineSeconds = ts.GetSeconds();
            if (rig && (motion || hand)) {
                const TransformComponent& transform = owner.GetComponent<TransformComponent>();
                glm::vec3 delta = transform.Translation - rig->PreviousPosition.value_or(transform.Translation);
                rig->PreviousPosition = transform.Translation;
                delta.y = 0.f;
                const glm::vec3 local = glm::inverse(glm::mat3(transform.GetTransform())) * delta;
                const ModelAnimationComponent::GaitStep step{rig->MovementRoot, glm::length(glm::vec2(local.x, local.z))};
                float advance = ts.GetSeconds();
                // 即使这一帧很慢，也让玩家先看到手碰到目标的姿势。通知仍按游戏时间发出。
                if (!compose && hand && hand->Timed && !hand->Passed && !hand->Released) {
                    advance = std::min(advance,std::max(0.f,hand->ReachAt-player.Time));
                    if (player.Time < hand->ReachAt) timelineSeconds = advance;
                }
                if (!hand || !hand->ShowContact) player.Update(renderer.ModelAsset, advance, [&](const ModelAnimation& animation, std::vector<ModelAnimation::NodePose>& pose) {
                    if (compose) montage->Apply(animation,pose);
                    if (motion) AdjustTerrainPose(*this,owner.GetUUID(),transform,*rig,*motion,animation,pose);
                    if (hand) AdjustHandPose(owner,*rig,*hand,animation,pose);
                }, &step, hand != nullptr || compose);
            } else if (compose) {
                player.Update(renderer.ModelAsset,ts.GetSeconds(),[&](const ModelAnimation& animation, std::vector<ModelAnimation::NodePose>& pose) {
                    montage->Apply(animation,pose);
                },nullptr,true);
            } else player.Update(renderer.ModelAsset, ts.GetSeconds());
            if (auto* animator = m_Registry.try_get<ActorAnimator>(handle)) {
                if (player.Clip != animator->Clip || !player.Playing) animator->Timeline.Cancel();
                else animator->Timeline.Advance(timelineSeconds);
            }
        });
        UpdateHandProps(*this,ts.GetSeconds());
    }

}
