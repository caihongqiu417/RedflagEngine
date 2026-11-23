#include"rfpch.h"
#include"Scene.h"
#include "Redflag/Assets/AssetManager.h"
#include "Redflag/AI/NpcSystem.h"
#include "Redflag/Gameplay/TerrainTraversal.h"
#include "Redflag/Gameplay/TraversalRig.h"
#include "Redflag/Animation/ActorAnimator.h"

#include"Redflag/Renderer/SceneRenderer.h"

#include"Redflag/ParticleSystem/ParticleSystem.h"

#include"Entity.h"
#include"ScriptableEntity.h"

//BOX2D
#include"box2d/b2_world.h"
#include"box2d/b2_fixture.h"
#include"box2d/b2_polygon_shape.h"
#include"box2d/b2_circle_shape.h"


namespace Redflag {

    static b2BodyType RigidBodyTypeToBox2DBody(RigidBody2DComponent::BodyType bodyType)
    {
        switch (bodyType)
        {
            case RigidBody2DComponent::BodyType::Static: return b2_staticBody;
            case RigidBody2DComponent::BodyType::Dynamic: return b2_dynamicBody;
            case RigidBody2DComponent::BodyType::Kinematic: return b2_kinematicBody;
        }

        RF_CORE_ASSERT(false, "Unknown body type");
        return b2_staticBody;
    }

	Scene::Scene()
	{
        m_Registry.on_destroy<ScriptComponent>().connect<&Scene::RemoveScript>(*this);
        m_Registry.on_destroy<NativeScriptComponent>().connect<&Scene::DestroyNativeScript>(*this);
        m_Registry.on_destroy<ParticleEmitterComponent>().connect<&Scene::DestroyEmitter>(*this);
        m_Registry.on_destroy<RigidBody2DComponent>().connect<&Scene::RemoveBody2D>(*this);
        m_Registry.on_destroy<BoxCollider2DComponent>().connect<&Scene::RemoveFixture2D<BoxCollider2DComponent>>(*this);
        m_Registry.on_destroy<CircleCollider2DComponent>().connect<&Scene::RemoveFixture2D<CircleCollider2DComponent>>(*this);
        m_Registry.on_update<BoxCollider2DComponent>().connect<&Scene::RemoveFixture2D<BoxCollider2DComponent>>(*this);
        m_Registry.on_update<CircleCollider2DComponent>().connect<&Scene::RemoveFixture2D<CircleCollider2DComponent>>(*this);
        m_Registry.on_destroy<RigidBody3DComponent>().connect<&Scene::RemovePhysicsBody>(*this);
        m_Registry.on_destroy<BoxCollider3DComponent>().connect<&Scene::RemovePhysicsBody>(*this);
        m_Registry.on_destroy<TerrainComponent>().connect<&Scene::RemoveTerrain>(*this);
        m_Registry.on_destroy<RagdollComponent>().connect<&Scene::RemoveRagdoll>(*this);
	}

	Scene::~Scene()
	{
        m_Registry.on_destroy<ScriptComponent>().disconnect(this);
        m_Registry.on_destroy<NativeScriptComponent>().disconnect(this);
        m_Registry.on_destroy<ParticleEmitterComponent>().disconnect(this);
        m_Registry.on_destroy<RigidBody2DComponent>().disconnect(this);
        m_Registry.on_destroy<BoxCollider2DComponent>().disconnect(this);
        m_Registry.on_destroy<CircleCollider2DComponent>().disconnect(this);
        m_Registry.on_update<BoxCollider2DComponent>().disconnect(this);
        m_Registry.on_update<CircleCollider2DComponent>().disconnect(this);
        m_Registry.on_destroy<RigidBody3DComponent>().disconnect(this);
        m_Registry.on_destroy<BoxCollider3DComponent>().disconnect(this);
        m_Registry.on_destroy<TerrainComponent>().disconnect(this);
        m_Registry.on_destroy<RagdollComponent>().disconnect(this);
	}

    template<typename Component>
    static Component CopySettings(const Component& source)
    {
        Component copy = source;
        if constexpr (std::is_same_v<Component, NativeScriptComponent>) copy.Instance = nullptr;
        if constexpr (std::is_same_v<Component, ParticleEmitterComponent>) copy.RuntimeParticleSystem = nullptr;
        return copy;
    }

    template<typename Component>
    static void CopyComponent(entt::registry& dst,entt::registry& src,const std::unordered_map<UUID,entt::entity>& enttMap)
    {
        auto view = src.view<Component>();
        for (auto e : view)
        {
            UUID uuid=src.get<IDComponent>(e).ID;
            RF_CORE_ASSERT(enttMap.find(uuid) != enttMap.end());

            entt::entity dstEnttID=enttMap.at(uuid);

            // “可拿取”只记录实体有没有这个标记，没有字段可复制。
            if constexpr (entt::component_traits<Component>::page_size == 0)
                dst.emplace_or_replace<Component>(dstEnttID);
            else
                dst.emplace_or_replace<Component>(dstEnttID, CopySettings(src.get<Component>(e)));
        }
    }

    template<typename Component>
    static void CopyComponentIfExist(Entity dst, Entity src)
    {
        if (src.HasComponent<Component>())
        {
            if constexpr (entt::component_traits<Component>::page_size == 0)
                dst.AddOrReplaceComponent<Component>();
            else
                dst.AddOrReplaceComponent<Component>(CopySettings(src.GetComponent<Component>()));
        }
    }

    Ref<Scene> Scene::Copy(Ref<Scene> other)
    {
        Ref<Scene> newScene = CreateRef<Scene>();

        newScene->m_ViewportWidth = other->m_ViewportWidth;
        newScene->m_ViewportHeight = other->m_ViewportHeight;
        newScene->m_Environment = other->m_Environment;
        newScene->m_SkyVisible = other->m_SkyVisible;

        auto& srcSceneRegistry = other->m_Registry;
        auto& dstSceneRegistry = newScene->m_Registry;
        std::unordered_map<UUID, entt::entity> enttMap;

        auto idView = srcSceneRegistry.view<IDComponent>();
        for (auto e : idView)
        {
            UUID uuid = srcSceneRegistry.get<IDComponent>(e).ID;
            const auto& name = srcSceneRegistry.get<TagComponent>(e).Tag;

            Entity newEntity = newScene->CreateEntityWithUUID(uuid, name);

            enttMap[uuid] = (entt::entity)newEntity;
        }

        CopyComponent<TransformComponent>(dstSceneRegistry, srcSceneRegistry, enttMap);
        CopyComponent<SpriteRendererComponent>(dstSceneRegistry, srcSceneRegistry, enttMap);
        CopyComponent<CircleRendererComponent>(dstSceneRegistry, srcSceneRegistry, enttMap);
        CopyComponent<CameraComponent>(dstSceneRegistry, srcSceneRegistry, enttMap);
        CopyComponent<NativeScriptComponent>(dstSceneRegistry, srcSceneRegistry, enttMap);
        CopyComponent<ScriptComponent>(dstSceneRegistry, srcSceneRegistry, enttMap);
        CopyComponent<RigidBody2DComponent>(dstSceneRegistry, srcSceneRegistry, enttMap);
        CopyComponent<BoxCollider2DComponent>(dstSceneRegistry, srcSceneRegistry, enttMap);
        CopyComponent<RigidBody3DComponent>(dstSceneRegistry, srcSceneRegistry, enttMap);
        CopyComponent<BoxCollider3DComponent>(dstSceneRegistry, srcSceneRegistry, enttMap);
        CopyComponent<PickupComponent>(dstSceneRegistry, srcSceneRegistry, enttMap);
        CopyComponent<HealthComponent>(dstSceneRegistry, srcSceneRegistry, enttMap);
        CopyComponent<InventoryComponent>(dstSceneRegistry, srcSceneRegistry, enttMap);
        CopyComponent<HudComponent>(dstSceneRegistry, srcSceneRegistry, enttMap);
        CopyComponent<InteractionComponent>(dstSceneRegistry, srcSceneRegistry, enttMap);
        CopyComponent<NpcComponent>(dstSceneRegistry, srcSceneRegistry, enttMap);
        CopyComponent<DecalComponent>(dstSceneRegistry, srcSceneRegistry, enttMap);
        CopyComponent<TerrainComponent>(dstSceneRegistry, srcSceneRegistry, enttMap);
        CopyComponent<FoliageComponent>(dstSceneRegistry, srcSceneRegistry, enttMap);
        CopyComponent<EnvironmentZoneComponent>(dstSceneRegistry, srcSceneRegistry, enttMap);
        CopyComponent<RagdollComponent>(dstSceneRegistry, srcSceneRegistry, enttMap);
        CopyComponent<SkeletonAnimationComponent>(dstSceneRegistry, srcSceneRegistry, enttMap);
        CopyComponent<CircleCollider2DComponent>(dstSceneRegistry, srcSceneRegistry, enttMap);
        CopyComponent<MeshRendererComponent>(dstSceneRegistry, srcSceneRegistry, enttMap);
        CopyComponent<ModelRendererComponent>(dstSceneRegistry, srcSceneRegistry, enttMap);
        CopyComponent<ModelAnimationComponent>(dstSceneRegistry, srcSceneRegistry, enttMap);
        CopyComponent<DirectionalLightComponent>(dstSceneRegistry, srcSceneRegistry, enttMap);
        CopyComponent<PointLightComponent>(dstSceneRegistry, srcSceneRegistry, enttMap);
        CopyComponent<ParticleEmitterComponent>(dstSceneRegistry, srcSceneRegistry, enttMap);
        CopyComponent<TextureAnimationComponent>(dstSceneRegistry, srcSceneRegistry, enttMap);

        // 属性面板可以在播放时改材质。复制可编辑的材质，原本共用一份材质的实体在副本里仍然共用。
        std::unordered_map<Material*, Ref<Material>> materials;
        auto copyMaterial = [&](Ref<Material>& material) {
            if (!material) return;
            Ref<Material>& copy = materials[material.get()];
            if (!copy) copy = material->Clone();
            material = copy;
        };
        dstSceneRegistry.view<MeshRendererComponent>().each([&](MeshRendererComponent& mesh) { copyMaterial(mesh.Mat); });
        dstSceneRegistry.view<ModelRendererComponent>().each([&](ModelRendererComponent& model) {
            for (Ref<Material>& material : model.OverrideMaterials) copyMaterial(material);
        });

        return newScene;
    }

	Entity Scene::CreateEntity(const std::string& name)
	{
        return CreateEntityWithUUID(UUID(), name);
	}

    Entity Scene::CreateEntityWithUUID(UUID uuid, const std::string& name)
    {
        Entity entity = { m_Registry.create(),this };
        entity.AddComponent<IDComponent>(uuid);
        entity.AddComponent<TransformComponent>();
        auto& tag = entity.AddComponent<TagComponent>();
        tag.Tag = name.empty() ? "Entity" : name;

        //UUID->entity映射表，因为C++和C#之间传递uuid
        m_EntityMap[uuid] = entity;

        return entity;
    }

    void Scene::DestroyEntity(Entity entity)
    {
		UUID entityID = entity.GetUUID();
		ScriptEngine::OnDestroyEntity(*this, entityID);
        if (entity.HasComponent<ScriptComponent>()) entity.RemoveComponent<ScriptComponent>();
        if (entity.HasComponent<NativeScriptComponent>()) entity.RemoveComponent<NativeScriptComponent>();
        m_Registry.destroy(entity);
		m_EntityMap.erase(entityID);
    }

    void Scene::OnRuntimeStart(uint16_t decisionPort)
    {
        ResetCharacterControls();
        OnPhysics2DStart();
        SyncPhysics3D();
        m_Npcs = CreateScope<NpcSystem>(*this, decisionPort);

        //脚本系统
        {
            //获取场景
            ScriptEngine::OnRuntimeStart(this);

            //初始化脚本实体
            auto view = m_Registry.view<ScriptComponent>();
            for (auto e : view)
            {
                Entity entity = { e,this };
                ScriptEngine::OnCreateEntity(entity);

            }
        }

        // 创建粒子系统运行时实例
        {
            auto view = m_Registry.view<ParticleEmitterComponent>();
            for (auto e : view)
            {
                auto& emitter = m_Registry.get<ParticleEmitterComponent>(e);
                emitter.RuntimeParticleSystem = new ParticleSystem(emitter.MaxParticles);
                emitter.EmissionAccumulator = 0.0f;
            }
        }

        // 重置纹理动画状态
        {
            auto view = m_Registry.view<TextureAnimationComponent>();
            for (auto e : view)
            {
                auto& anim = m_Registry.get<TextureAnimationComponent>(e);
                anim.CurrentFrame = 0;
                anim.Timer = 0.0f;
            }
        }
    }

    void Scene::RemoveScript(entt::entity handle)
    {
        ScriptEngine::OnRemoveScript(*this, m_Registry.get<IDComponent>(handle).ID);
    }

    void Scene::DestroyNativeScript(entt::entity handle)
    {
        NativeScriptComponent& script = m_Registry.get<NativeScriptComponent>(handle);
        if (!script.Instance) return;
        script.Instance->OnDestroy();
        script.DestroyScript(&script);
    }

    void Scene::DestroyEmitter(entt::entity handle)
    {
        ParticleEmitterComponent& emitter = m_Registry.get<ParticleEmitterComponent>(handle);
        delete static_cast<ParticleSystem*>(emitter.RuntimeParticleSystem);
        emitter.RuntimeParticleSystem = nullptr;
    }

    void Scene::OnRuntimeStop()
    {
        m_Npcs.reset();
        for (entt::entity handle : m_Registry.view<NativeScriptComponent>()) DestroyNativeScript(handle);
        ScriptEngine::OnRuntimeStop();
        for (entt::entity handle : m_Registry.view<ParticleEmitterComponent>()) DestroyEmitter(handle);

        StopPhysics3D();
        OnPhysics2DStop();
    }

    void Scene::OnSimulationStart()
    {
        ResetCharacterControls();
        OnPhysics2DStart();
        SyncPhysics3D();
    }

    void Scene::OnSimulationStop()
    {
        StopPhysics3D();
        OnPhysics2DStop();
    }

    void Scene::OnUpdateEditor(Timestep ts,EditorCamera& camera)
    {
        OnUpdateAnimations(ts);
        SceneRenderer::RenderScene(*this, camera);
    }

    void Scene::ResetCharacterControls()
    {
        m_Registry.clear<TerrainTraversal>();
        m_Registry.clear<ActorAnimator>();
        m_Registry.view<SkeletonAnimationComponent>().each([](SkeletonAnimationComponent& player) { player.RestartControl(); });
    }

    bool Scene::SetCharacterMovement(UUID id, glm::vec2 direction)
    {
        Entity actor = GetEntityByUUID(id);
        if (!actor || !actor.HasComponent<SkeletonAnimationComponent>()) return false;
        SkeletonAnimationComponent& player = actor.GetComponent<SkeletonAnimationComponent>();
        if (!player.HasControl()) return false;
        player.Movement = glm::length(direction) > 1.f ? glm::normalize(direction) : direction;
        return true;
    }

    bool Scene::FaceCharacter(UUID id, glm::vec2 direction)
    {
        Entity actor = GetEntityByUUID(id);
        if (!actor || !actor.HasComponent<SkeletonAnimationComponent>() || direction == glm::vec2(0)) return false;
        actor.GetComponent<TransformComponent>().Rotation.y = std::atan2(direction.x, direction.y);
        return true;
    }

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

        m_Registry.view<ModelRendererComponent, ModelAnimationComponent>().each([&](entt::entity handle, ModelRendererComponent& renderer, ModelAnimationComponent& player) {
            if (!renderer.ModelAsset && renderer.ModelID) renderer.ModelAsset = AssetManager::Load<Model>(renderer.ModelID);
            auto* rig = m_Registry.try_get<TraversalRig>(handle);
            const auto* motion = m_Registry.try_get<TerrainTraversal>(handle);
            if (rig && motion) {
                Entity owner(handle,this);
                const TransformComponent& transform = owner.GetComponent<TransformComponent>();
                glm::vec3 delta = transform.Translation - rig->PreviousPosition.value_or(transform.Translation);
                rig->PreviousPosition = transform.Translation;
                delta.y = 0.f;
                const glm::vec3 local = glm::inverse(glm::mat3(transform.GetTransform())) * delta;
                const ModelAnimationComponent::GaitStep step{rig->MovementRoot, glm::length(glm::vec2(local.x, local.z))};
                player.Update(renderer.ModelAsset, ts.GetSeconds(), [&](const ModelAnimation& animation, std::vector<ModelAnimation::NodePose>& pose) {
                    AdjustTerrainPose(*this,owner.GetUUID(),transform,*rig,*motion,animation,pose);
                }, &step);
            } else player.Update(renderer.ModelAsset, ts.GetSeconds());
            if (auto* animator = m_Registry.try_get<ActorAnimator>(handle)) {
                if (player.Clip != animator->Clip || !player.Playing) animator->Timeline.Cancel();
                else animator->Timeline.Advance(ts.GetSeconds());
            }
        });
    }

    // ---- 游戏脚本、动画与物理 ----
    void Scene::UpdateGame(Timestep ts, glm::vec2 movement)
    {
        // 脚本先给各自的角色设置方向，本帧骨骼和物理使用刚收到的输入。
        {
            auto view = m_Registry.view<ScriptComponent>();
            for (auto e : view)
            {
                Entity entity = { e,this };
                ScriptEngine::OnUpdateEntity(entity, ts);
            }

            m_Registry.view<NativeScriptComponent>().each([=](auto entity, auto& nsc)
            {
                if (!nsc.Instance)
                {
                    nsc.Instance = nsc.InstantiateScript();
                    nsc.Instance->m_Entity = Entity{ entity,this };
                    nsc.Instance->OnCreate();
                }
                nsc.Instance->OnUpdate(ts);
            });
        }

        // ---- 粒子发射 ----
        {
            auto view = m_Registry.view<TransformComponent, ParticleEmitterComponent>();
            for (auto e : view)
            {
                auto& transform = m_Registry.get<TransformComponent>(e);
                auto& emitter   = m_Registry.get<ParticleEmitterComponent>(e);
                if (!emitter.RuntimeParticleSystem)
                    continue;

                ParticleSystem* ps = (ParticleSystem*)emitter.RuntimeParticleSystem;

                if (emitter.Active)
                {
                    emitter.EmissionAccumulator += emitter.EmissionRate * ts;
                    while (emitter.EmissionAccumulator >= 1.0f)
                    {
                        ParticleProps props;
                        props.Position          = { transform.Translation.x, transform.Translation.y };
                        props.ColorBegin        = emitter.ColorBegin;
                        props.ColorEnd          = emitter.ColorEnd;
                        props.SizeBegin         = emitter.SizeBegin;
                        props.SizeEnd           = emitter.SizeEnd;
                        props.SizeVariation     = emitter.SizeVariation;
                        props.Velocity          = emitter.Velocity;
                        props.VelocityVariation = emitter.VelocityVariation;
                        props.LifeTime          = emitter.LifeTime;
                        ps->Emit(props);
                        emitter.EmissionAccumulator -= 1.0f;
                    }
                }

                ps->OnUpdate(ts);
            }
        }

        // ----------
        // 更新纹理动画
        // ----------
        {
            auto view = m_Registry.view<TextureAnimationComponent>();
            for (auto e : view)
            {
                auto& anim = m_Registry.get<TextureAnimationComponent>(e);
                if (anim.FrameCount <= 1 || anim.AnimationSpeed <= 0.0f)
                    continue;

                anim.Timer += ts;
                float frameDuration = 1.0f / anim.AnimationSpeed;
                while (anim.Timer >= frameDuration)
                {
                    anim.Timer -= frameDuration;
                    anim.CurrentFrame++;
                    if (anim.CurrentFrame >= anim.FrameCount)
                        anim.CurrentFrame = anim.Loop ? 0 : anim.FrameCount - 1;
                }
            }
        }

        if (m_Npcs)
            m_Npcs->Update(ts);

        OnUpdateAnimations(ts, movement);
        OnUpdatePhysics(ts);
    }

    void Scene::OnUpdateRuntime(Timestep ts, glm::vec2 movement)
    {
        UpdateGame(ts, movement);

        Camera* mainCamera = nullptr;
        glm::mat4 cameraTransform;

        auto cameraView = m_Registry.view<TransformComponent, CameraComponent>();
        cameraView.each([&](auto entity, TransformComponent& transform, CameraComponent& camera) {
            if (camera.Primary)
            {
                mainCamera = &camera.Camera;
                cameraTransform = transform.GetTransform();
            }
            });

        // ----------
       // 渲染委托给 SceneRenderer
       // ----------
        if (mainCamera)
        {
            // 查找主相机是「场景逻辑」，所以留在 Scene 里。
            // 但实际绘制由 SceneRenderer 负责。
            SceneRenderer::RenderRuntime(*this, mainCamera->GetProjection(), cameraTransform);
        }
    }

    void Scene::OnUpdateSimulation(Timestep ts, EditorCamera& camera, glm::vec2 movement)
    {
        OnUpdateAnimations(ts, movement);
        OnUpdatePhysics(ts);
        SceneRenderer::RenderScene(*this, camera);
    }

    void Scene::OnViewportResize(uint32_t width, uint32_t height)
    {
        m_ViewportWidth = width;
        m_ViewportHeight = height;

        auto view = m_Registry.view<CameraComponent>();
        for (auto entity : view)
        {
            auto& cameraComponent=view.get<CameraComponent>(entity);
            if (!cameraComponent.FixedAspectRatio)
            {
                cameraComponent.Camera.SetViewportSize(width, height);
            }
        }
    }

    void Scene::DuplicateEntity(Entity entity)
    {
        Entity newEntity = CreateEntity(entity.GetName());

        newEntity.GetComponent<TransformComponent>() = entity.GetComponent<TransformComponent>();
        CopyComponentIfExist<SpriteRendererComponent>(newEntity, entity);
        CopyComponentIfExist<CircleRendererComponent>(newEntity, entity);
        CopyComponentIfExist<CameraComponent>(newEntity, entity);
        CopyComponentIfExist<RigidBody2DComponent>(newEntity, entity);
        CopyComponentIfExist<BoxCollider2DComponent>(newEntity, entity);
        CopyComponentIfExist<RigidBody3DComponent>(newEntity, entity);
        CopyComponentIfExist<BoxCollider3DComponent>(newEntity, entity);
        CopyComponentIfExist<PickupComponent>(newEntity, entity);
        CopyComponentIfExist<HealthComponent>(newEntity, entity);
        CopyComponentIfExist<InventoryComponent>(newEntity, entity);
        CopyComponentIfExist<HudComponent>(newEntity, entity);
        CopyComponentIfExist<InteractionComponent>(newEntity, entity);
        CopyComponentIfExist<NpcComponent>(newEntity, entity);
        CopyComponentIfExist<DecalComponent>(newEntity, entity);
        CopyComponentIfExist<TerrainComponent>(newEntity, entity);
        CopyComponentIfExist<FoliageComponent>(newEntity, entity);
        CopyComponentIfExist<EnvironmentZoneComponent>(newEntity, entity);
        CopyComponentIfExist<RagdollComponent>(newEntity, entity);
        CopyComponentIfExist<SkeletonAnimationComponent>(newEntity, entity);
        CopyComponentIfExist<CircleCollider2DComponent>(newEntity, entity);
        CopyComponentIfExist<NativeScriptComponent>(newEntity, entity);
        CopyComponentIfExist<ScriptComponent>(newEntity, entity);
        CopyComponentIfExist<MeshRendererComponent>(newEntity, entity);
        if (newEntity.HasComponent<MeshRendererComponent>())
        {
            auto& mrc = newEntity.GetComponent<MeshRendererComponent>();
            if (mrc.Mat)
                mrc.Mat = mrc.Mat->Clone();
        }

        CopyComponentIfExist<ModelRendererComponent>(newEntity, entity);
        CopyComponentIfExist<ModelAnimationComponent>(newEntity, entity);
        if (newEntity.HasComponent<ModelRendererComponent>())
        {
            auto& mrc = newEntity.GetComponent<ModelRendererComponent>();
            for (auto& mat : mrc.OverrideMaterials)
                if (mat)
                    mat = mat->Clone();
        }
        CopyComponentIfExist<DirectionalLightComponent>(newEntity, entity);
        CopyComponentIfExist<PointLightComponent>(newEntity, entity);
        CopyComponentIfExist<ParticleEmitterComponent>(newEntity, entity);
        CopyComponentIfExist<TextureAnimationComponent>(newEntity, entity);
    }

    Entity Scene::GetEntityByUUID(UUID uuid)
    {
        if (m_EntityMap.find(uuid) != m_EntityMap.end())
            return{ m_EntityMap.at(uuid),this };

        return {};
    }

    Entity Scene::GetPrimaryCameraEntity()
    {
        auto view = m_Registry.view<CameraComponent>();
        for (auto entity : view)
        {
            const auto& camera = view.get<CameraComponent>(entity);
            if (camera.Primary)
                return Entity{ entity,this };
        }
        return{};
    }

    // ---- 物理模拟与空间查询 ----

    void Scene::OnUpdatePhysics(Timestep ts)
    {
        SyncPhysics2D();
        const int32_t velocityIterations = 6, positionIterations = 2;
        m_PhysicsWorld->Step(ts, velocityIterations, positionIterations);
        for (const auto& [handle, runtime] : m_Bodies2D)
        {
            TransformComponent& transform = m_Registry.get<TransformComponent>(handle);
            const b2Vec2& position = runtime.Body->GetPosition();
            transform.Translation.x = position.x;
            transform.Translation.y = position.y;
            transform.Rotation.z = runtime.Body->GetAngle();
        }

        SyncPhysics3D();
        if (!m_Physics3D)
            return;

        // 一帧可能跨过两个物理步。每步都要按当时的脚底位置决定关节用力。
        m_Physics3D->Step(ts.GetSeconds(), [&](float step) {
            for (const auto& [handle, ragdoll] : m_Ragdolls)
            {
                const RagdollComponent& settings = m_Registry.get<RagdollComponent>(handle);
                if (settings.Walking)
                    ragdoll->DriveWalk(step, settings.Speed);
                else
                    ragdoll->Relax();
            }
        });
        for (const auto& [handle, box] : m_Boxes3D)
        {
            if (box.Type != BodyType3D::Dynamic)
                continue;
            TransformComponent& transform = m_Registry.get<TransformComponent>(handle);
            glm::quat rotation;
            if (m_Physics3D->GetPose(box.Entity, transform.Translation, rotation))
                transform.Rotation = glm::eulerAngles(rotation);
        }
        for (const auto& [handle, ragdoll] : m_Ragdolls)
        {
            TransformComponent& transform = m_Registry.get<TransformComponent>(handle);
            glm::quat rotation;
            ragdoll->GetPose(Ragdoll::Pelvis, transform.Translation, rotation);
            transform.Rotation = glm::eulerAngles(rotation);
        }
    }

    void Scene::SyncPhysics3D()
    {
        auto boxes = m_Registry.view<IDComponent, TransformComponent, RigidBody3DComponent, BoxCollider3DComponent>();
        for (entt::entity handle : boxes)
        {
            const auto& [id, transform, rigidBody, collider] = boxes.get(handle);
            BoxBody3D box{ id.ID, rigidBody.Type, transform.Translation, glm::quat(transform.Rotation),
                glm::abs(transform.Scale) * collider.HalfSize, rigidBody.Density, transform.Scale * collider.Center };

            // 压扁到零厚度的盒子没有体积。恢复尺寸后，下次物理步会重新加入它。
            if (!glm::all(glm::greaterThan(box.HalfSize, glm::vec3(0))) ||
                (box.Type != BodyType3D::Static && !(box.Density > 0.f)) ||
                m_Registry.any_of<RigidBody2DComponent, TerrainComponent>(handle))
            {
                RemovePhysicsBody(handle);
                continue;
            }

            auto existing = m_Boxes3D.find(handle);
            if (existing != m_Boxes3D.end())
            {
                BoxBody3D& old = existing->second;
                // 动态箱子的位置由 PhysX 算。每帧按新位置重建它，会把下落速度清零。
                const bool moved = box.Position != old.Position || box.Rotation != old.Rotation;
                if (old.Type == box.Type && old.HalfSize == box.HalfSize && old.Center == box.Center && old.Density == box.Density &&
                    !(box.Type == BodyType3D::Static && moved))
                {
                    if (box.Type == BodyType3D::Kinematic && moved)
                    {
                        m_Physics3D->SetKinematicTarget(box.Entity, box.Position, box.Rotation);
                        old.Position = box.Position;
                        old.Rotation = box.Rotation;
                    }
                    continue;
                }
                RemovePhysicsBody(handle);
            }

            if (!m_Physics3D)
                m_Physics3D = CreateScope<PhysicsWorld>();
            m_Physics3D->AddBox(box);
            m_Boxes3D.emplace(handle, box);
        }

        auto characters = m_Registry.view<IDComponent, TransformComponent, RagdollComponent>();
        for (entt::entity handle : characters)
        {
            if (m_Registry.any_of<RigidBody2DComponent, RigidBody3DComponent, TerrainComponent>(handle))
            {
                RemoveRagdoll(handle);
                continue;
            }
            if (m_Ragdolls.count(handle)) continue;
            if (!m_Physics3D) m_Physics3D = CreateScope<PhysicsWorld>();

            const TransformComponent& transform = characters.get<TransformComponent>(handle);
            glm::quat rotation(transform.Rotation);
            glm::vec3 feet = transform.Translation - rotation * Ragdoll::Center(Ragdoll::Pelvis);
            Ragdoll::Entities limbs;
            for (uint64_t& id : limbs) id = UUID();
            uint64_t owner = characters.get<IDComponent>(handle).ID;
            m_Ragdolls.emplace(handle, CreateScope<Ragdoll>(*m_Physics3D, limbs, feet, rotation, owner));
        }
        SyncTerrain();
    }

    void Scene::SyncTerrain()
    {
        auto ground = m_Registry.view<IDComponent, TransformComponent, TerrainComponent>();
        for (entt::entity handle : ground)
        {
            const TerrainComponent& field = ground.get<TerrainComponent>(handle);
            const TransformComponent& pose = ground.get<TransformComponent>(handle);
            Ref<Terrain> data = field.Collision ? field.GetTerrain() : nullptr;
            if (!data || field.CellSize <= 0.f || field.HeightScale <= 0.f ||
                glm::any(glm::lessThanEqual(pose.Scale, glm::vec3(0.f))))
            {
                RemoveTerrain(handle);
                continue;
            }

            const auto found = m_TerrainBodies.find(handle);
            if (found != m_TerrainBodies.end())
            {
                const TerrainBody& old = found->second;
                if (old.Data == data && old.Position == pose.Translation && old.Rotation == pose.Rotation &&
                    old.Scale == pose.Scale && old.CellSize == field.CellSize && old.HeightScale == field.HeightScale) continue;
            }
            if (!m_Physics3D) m_Physics3D = CreateScope<PhysicsWorld>();
            const uint64_t id = ground.get<IDComponent>(handle).ID;
            m_Physics3D->AddTerrain(id, *data, pose.Translation, glm::quat(pose.Rotation), pose.Scale, field.CellSize, field.HeightScale);
            m_TerrainBodies[handle] = {id, std::move(data), pose.Translation, pose.Rotation, pose.Scale, field.CellSize, field.HeightScale};
        }
    }

    void Scene::RemoveTerrain(entt::entity handle)
    {
        const auto found = m_TerrainBodies.find(handle);
        if (found == m_TerrainBodies.end()) return;
        m_Physics3D->Remove(found->second.Entity);
        m_TerrainBodies.erase(found);
    }

    void Scene::RemovePhysicsBody(entt::entity handle)
    {
        auto existing = m_Boxes3D.find(handle);
        if (existing != m_Boxes3D.end())
        {
            m_Physics3D->Remove(existing->second.Entity);
            m_Boxes3D.erase(existing);
        }
    }

    void Scene::StopPhysics3D()
    {
        m_Ragdolls.clear();
        m_Boxes3D.clear();
        m_TerrainBodies.clear();
        m_Physics3D.reset();
    }

    const Ragdoll* Scene::GetRagdoll(entt::entity handle) const
    {
        auto found = m_Ragdolls.find(handle);
        return found == m_Ragdolls.end() ? nullptr : found->second.get();
    }

    void Scene::RemoveRagdoll(entt::entity handle)
    {
        m_Ragdolls.erase(handle);
    }

    std::array<glm::mat4, Ragdoll::Count> Scene::GetRagdollMeshes(entt::entity handle) const
    {
        const TransformComponent& root = m_Registry.get<TransformComponent>(handle);
        const Ragdoll* body = GetRagdoll(handle);
        std::array<glm::mat4, Ragdoll::Count> meshes;
        for (int index = 0; index < Ragdoll::Count; ++index)
        {
            Ragdoll::Limb limb = static_cast<Ragdoll::Limb>(index);
            glm::quat rotation(root.Rotation);
            glm::vec3 position = root.Translation + rotation * (Ragdoll::Center(limb) - Ragdoll::Center(Ragdoll::Pelvis));
            if (body) body->GetPose(limb, position, rotation);
            meshes[index] = glm::translate(glm::mat4(1), position) * glm::mat4_cast(rotation) *
                glm::scale(glm::mat4(1), 2.f * Ragdoll::HalfSize(limb));
        }
        return meshes;
    }

    std::optional<RayHit3D> Scene::Raycast(glm::vec3 origin, glm::vec3 direction, float maxDistance, std::optional<uint64_t> ignore) const
    {
        if (!m_Physics3D)
            return std::nullopt;
        return m_Physics3D->Raycast(origin, direction, maxDistance, ignore);
    }

    float Scene::SweepSphere(glm::vec3 center, glm::vec3 direction, float radius, float distance, uint64_t ignore) const
    {
        return m_Physics3D ? m_Physics3D->SweepSphere(center, direction, radius, distance, ignore) : distance;
    }

    std::vector<uint64_t> Scene::OverlapSphere(glm::vec3 center, float radius) const
    {
        if (!m_Physics3D)
            return {};
        return m_Physics3D->OverlapSphere(center, radius);
    }

    void Scene::OnPhysics2DStart()
    {
        m_Bodies2D.clear();
        m_PhysicsWorld = CreateScope<b2World>(b2Vec2(0.f, -9.8f));
        SyncPhysics2D();
    }

    template<typename Collider>
    static b2Fixture* CreateFixture2D(b2Body& body, const Collider& collider, const b2Shape& shape)
    {
        b2FixtureDef fixture;
        fixture.shape = &shape;
        fixture.density = collider.Density;
        fixture.friction = collider.Friction;
        fixture.restitution = collider.Restitution;
        fixture.restitutionThreshold = collider.RestitutionThreshold;
        return body.CreateFixture(&fixture);
    }

    void Scene::SyncPhysics2D()
    {
        // 添加组件后，调用者还要填写类型和尺寸。到物理步开始时再创建刚体和碰撞形状。
        auto bodies = m_Registry.view<RigidBody2DComponent, TransformComponent>();
        for (entt::entity handle : bodies)
        {
            const auto& [rigidBody, transform] = bodies.get(handle);
            Body2D& runtime = m_Bodies2D[handle];
            b2BodyType type = RigidBodyTypeToBox2DBody(rigidBody.Type);
            if (!runtime.Body)
            {
                b2BodyDef def;
                def.type = type;
                def.position.Set(transform.Translation.x, transform.Translation.y);
                def.angle = transform.Rotation.z;
                def.fixedRotation = rigidBody.FixedRotation;
                runtime.Body = m_PhysicsWorld->CreateBody(&def);
            }
            else
            {
                runtime.Body->SetType(type);
                runtime.Body->SetFixedRotation(rigidBody.FixedRotation);
            }

            const BoxCollider2DComponent* box = m_Registry.try_get<BoxCollider2DComponent>(handle);
            if (box && !runtime.Box)
            {
                b2PolygonShape shape;
                shape.SetAsBox(transform.Scale.x * box->Size.x, transform.Scale.y * box->Size.y);
                runtime.Box = CreateFixture2D(*runtime.Body, *box, shape);
            }

            const CircleCollider2DComponent* circle = m_Registry.try_get<CircleCollider2DComponent>(handle);
            if (circle && !runtime.Circle)
            {
                b2CircleShape shape;
                shape.m_p.Set(circle->Offset.x, circle->Offset.y);
                shape.m_radius = glm::max(transform.Scale.x, transform.Scale.y) * circle->Radius;
                runtime.Circle = CreateFixture2D(*runtime.Body, *circle, shape);
            }
        }
    }

    void Scene::RemoveBody2D(entt::entity handle)
    {
        auto body = m_Bodies2D.find(handle);
        if (body != m_Bodies2D.end())
        {
            // Box2D 删除刚体时也会删除附着在它上面的所有形状。
            m_PhysicsWorld->DestroyBody(body->second.Body);
            m_Bodies2D.erase(body);
        }
    }

    template<typename Collider>
    void Scene::RemoveFixture2D(entt::entity handle)
    {
        auto body = m_Bodies2D.find(handle);
        if (body == m_Bodies2D.end())
            return;
        b2Fixture*& fixture = std::is_same_v<Collider, BoxCollider2DComponent> ? body->second.Box : body->second.Circle;
        if (fixture)
        {
            body->second.Body->DestroyFixture(fixture);
            fixture = nullptr;
        }
    }

    void Scene::OnPhysics2DStop()
    {
        m_Bodies2D.clear();
        m_PhysicsWorld.reset();
    }

    void Scene::OnCameraAdded(CameraComponent& component)
    {
        component.Camera.SetViewportSize(m_ViewportWidth, m_ViewportHeight);
    }
}
