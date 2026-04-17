#include"rfpch.h"
#include"Scene.h"

// 渲染职责已转移到 SceneRenderer；Scene 不再直接调用 Renderer2D / Renderer3D。
// 保留 Renderer2D.h 仅用于 ResetStats（ForgeLayer 在 OnUpdate 中调用），
// 后续这个依赖也会移走。
#include"Renderer/Renderer2D.h"
#include"Renderer/Renderer3D.h"
#include"Redflag/Renderer/SceneRenderer.h"

#include"Redflag/Core/AssetManager.h"
#include"Redflag/Renderer/Model.h"
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

	}

	Scene::~Scene()
	{

	}

    template<typename Component>
    static void CopyComponent(entt::registry& dst,entt::registry& src,const std::unordered_map<UUID,entt::entity> enttMap)
    {
        auto view = src.view<Component>();
        for (auto e : view)
        {
            UUID uuid=src.get<IDComponent>(e).ID;
            RF_CORE_ASSERT(enttMap.find(uuid) != enttMap.end());

            entt::entity dstEnttID=enttMap.at(uuid);

            auto& component = src.get<Component>(e);
            dst.emplace_or_replace<Component>(dstEnttID,component);
        }
    }

    template<typename Component>
    static void CopyComponentIfExist(Entity dst, Entity src)
    {
        if (src.HasComponent<Component>())
            dst.AddOrReplaceComponent<Component>(src.GetComponent<Component>());
    }

    Ref<Scene> Scene::Copy(Ref<Scene> other)
    {
        Ref<Scene> newScene = CreateRef<Scene>();

        newScene->m_ViewportWidth = other->m_ViewportWidth;
        newScene->m_ViewportHeight = other->m_ViewportHeight;

        auto& srcSceneRegistry = other->m_Registry;
        auto& dstSceneRegistry = newScene->m_Registry;
        std::unordered_map<UUID, entt::entity> enttMap;

        //����ʵ��
        auto idView = srcSceneRegistry.view<IDComponent>();
        for (auto e : idView)
        {
            UUID uuid = srcSceneRegistry.get<IDComponent>(e).ID;
            const auto& name = srcSceneRegistry.get<TagComponent>(e).Tag;
            // �ؼ������� CreateEntityWithUUID����������ɣ�ʹ�ñ���� UUID
            Entity newEntity = newScene->CreateEntityWithUUID(uuid, name);

            // ��¼ӳ���ϵ��UUID -> ��ʵ��� entt ���
            enttMap[uuid] = (entt::entity)newEntity;
        }

        // ���������ͨ��ӳ����ҵ� UUID ��ͬ��Ŀ��ʵ�壬Ȼ���������ݿ���ȥ
        CopyComponent<TransformComponent>(dstSceneRegistry, srcSceneRegistry, enttMap);
        CopyComponent<SpriteRendererComponent>(dstSceneRegistry, srcSceneRegistry, enttMap);
        CopyComponent<CircleRendererComponent>(dstSceneRegistry, srcSceneRegistry, enttMap);
        CopyComponent<CameraComponent>(dstSceneRegistry, srcSceneRegistry, enttMap);
        CopyComponent<NativeScriptComponent>(dstSceneRegistry, srcSceneRegistry, enttMap);
        CopyComponent<RigidBody2DComponent>(dstSceneRegistry, srcSceneRegistry, enttMap);
        CopyComponent<BoxCollider2DComponent>(dstSceneRegistry, srcSceneRegistry, enttMap);
        CopyComponent<CircleCollider2DComponent>(dstSceneRegistry, srcSceneRegistry, enttMap);
        CopyComponent<MeshRendererComponent>(dstSceneRegistry, srcSceneRegistry, enttMap);
        CopyComponent<ModelRendererComponent>(dstSceneRegistry, srcSceneRegistry, enttMap);
        CopyComponent<DirectionalLightComponent>(dstSceneRegistry, srcSceneRegistry, enttMap);
        CopyComponent<PointLightComponent>(dstSceneRegistry, srcSceneRegistry, enttMap);
        CopyComponent<ParticleEmitterComponent>(dstSceneRegistry, srcSceneRegistry, enttMap);
        CopyComponent<TextureAnimationComponent>(dstSceneRegistry, srcSceneRegistry, enttMap);

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
        return entity;
    }

    void Scene::DestroyEntity(Entity entity)
    {
        m_Registry.destroy(entity);
    }

    void Scene::OnRuntimeStart()
    {
        OnPhysics2DStart();

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

    void Scene::OnRuntimeStop()
    {
        OnPhysics2DStop();

        // 销毁粒子系统运行时实例
        {
            auto view = m_Registry.view<ParticleEmitterComponent>();
            for (auto e : view)
            {
                auto& emitter = m_Registry.get<ParticleEmitterComponent>(e);
                if (emitter.RuntimeParticleSystem)
                {
                    delete (ParticleSystem*)emitter.RuntimeParticleSystem;
                    emitter.RuntimeParticleSystem = nullptr;
                }
            }
        }
    }

    void Scene::OnSimulationStart()
    {
        OnPhysics2DStart();
    }

    void Scene::OnSimulationStop()
    {
        OnPhysics2DStop();
    }

    void Scene::OnUpdateEditor(Timestep ts,EditorCamera& camera)
    {
        // ======================================================================
        // 渲染职责委托给 SceneRenderer
        //
        // 以前这里直接遍历 ECS 并调用 Renderer2D / Renderer3D，
        // 现在统一由 SceneRenderer::RenderScene 负责。
        //
        // Scene 只持有数据（registry），不再决定「怎么画」。
        // 这样后续加 Shadow / Deferred / PostProcess 等 pass 时，
        // 只需要修改 SceneRenderer，不用动 Scene。
        // ======================================================================
        SceneRenderer::RenderScene(*this, camera);
    }

    //runtime:�ӳ������������
    void Scene::OnUpdateRuntime(Timestep ts)
    {   //===============================================================
        //=========================���½ű�==============================
        //===============================================================
        {
            m_Registry.view<NativeScriptComponent>().each([=](auto entity, auto& nsc)
            {
                    // entity: ��ǰ��������ʵ��
                     // nsc: ���ʵ���ϵ� NativeScriptComponent
                if (!nsc.Instance)
                {
                    nsc.Instance = nsc.InstantiateScript();
                    nsc.Instance->m_Entity = Entity{ entity,this };
                    nsc.Instance->OnCreate();
                }
                nsc.Instance->OnUpdate(ts);
            });
        }

        //================================================================
        //==================== 更新粒子发射器 ==========================
        //================================================================
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

        //================================================================
        //==================== 更新纹理动画 ============================
        //================================================================
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

        // ����
        // �ű�ʩ����/�ٶ� �� ������������ײ��λ�� �� ��Ⱦͬ���任��
        // ��ֹ��������ű����������ӳ�
        {
            const int32_t velocityIterations = 6;
            const int32_t positionIterations = 2;
            m_PhysicsWorld->Step(ts, velocityIterations, positionIterations);

            //��box2D��ȡ�任
            auto view = m_Registry.view<RigidBody2DComponent>();
            for (auto e : view)
            {
                Entity entity = { e,this };
                auto& transform = entity.GetComponent<TransformComponent>();
                auto& rb2d = entity.GetComponent<RigidBody2DComponent>();
                
                // ȡ��֮ǰ�洢�����µĸ���ָ�룬�ø���ָ�뽫���������λ��/�Ƕ�ͬ���� Transform
                b2Body* body = (b2Body*)rb2d.RuntimeBody;
                const auto& position = body->GetPosition();
                transform.Translation.x = position.x;
                transform.Translation.y = position.y;
                transform.Rotation.z = body->GetAngle();
            }
        }

        
        //===============================================================
        //=========================�������==============================
        //===============================================================
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

        //===============================================================
       //============== 渲染委托给 SceneRenderer =======================
       //===============================================================
        if (mainCamera)
        {
            // 查找主相机是「场景逻辑」，所以留在 Scene 里。
            // 但实际绘制由 SceneRenderer 负责。
            SceneRenderer::RenderRuntime(*this, mainCamera->GetProjection(), cameraTransform);
        }
    }

    void Scene::OnUpdateSimulation(Timestep ts, EditorCamera& camera)
    {
        // 步进物理（不执行 NativeScript，这是与 OnUpdateRuntime 的核心区别）
        {
            const int32_t velocityIterations = 6;
            const int32_t positionIterations = 2;
            m_PhysicsWorld->Step(ts, velocityIterations, positionIterations);

            // 将 Box2D 计算结果同步回 TransformComponent
            auto view = m_Registry.view<RigidBody2DComponent>();
            for (auto e : view)
            {
                Entity entity = { e, this };
                auto& transform = entity.GetComponent<TransformComponent>();
                auto& rb2d = entity.GetComponent<RigidBody2DComponent>();

                b2Body* body = (b2Body*)rb2d.RuntimeBody;
                const auto& position = body->GetPosition();
                transform.Translation.x = position.x;
                transform.Translation.y = position.y;
                transform.Rotation.z = body->GetAngle();
            }
        }

        // 用 EditorCamera 渲染（与 OnUpdateEditor 相同的渲染路径）
        OnUpdateEditor(ts, camera);
    }

    void Scene::OnViewportResize(uint32_t width, uint32_t height)
    {
        m_ViewportWidth = width;
        m_ViewportHeight = height;

        auto view = m_Registry.view<CameraComponent>();
        for (auto entity : view)
        {
            auto& cameraComponent=view.get<CameraComponent>(entity);
            if (cameraComponent.FixedAspectRatio)
            {
                cameraComponent.Camera.SetViewportSize(width, height);
            }
        }
    }

    void Scene::DuplicateEntity(Entity entity)
    {
        Entity newEntity = CreateEntity(entity.GetName());

        CopyComponentIfExist<TransformComponent>(newEntity, entity);
        CopyComponentIfExist<SpriteRendererComponent>(newEntity, entity);
        CopyComponentIfExist<CircleRendererComponent>(newEntity, entity);
        CopyComponentIfExist<CameraComponent>(newEntity, entity);
        CopyComponentIfExist<RigidBody2DComponent>(newEntity, entity);
        CopyComponentIfExist<BoxCollider2DComponent>(newEntity, entity);
        CopyComponentIfExist<CircleCollider2DComponent>(newEntity, entity);
        CopyComponentIfExist<NativeScriptComponent>(newEntity, entity);
        CopyComponentIfExist<MeshRendererComponent>(newEntity, entity);
        CopyComponentIfExist<ModelRendererComponent>(newEntity, entity);
        CopyComponentIfExist<DirectionalLightComponent>(newEntity, entity);
        CopyComponentIfExist<PointLightComponent>(newEntity, entity);
        CopyComponentIfExist<ParticleEmitterComponent>(newEntity, entity);
        CopyComponentIfExist<TextureAnimationComponent>(newEntity, entity);
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

    void Scene::OnPhysics2DStart()
    {
        m_PhysicsWorld = new b2World({ 0.f,-9.8f });
        auto view = m_Registry.view<RigidBody2DComponent>();
        for (auto e : view)
        {
            Entity entity = { e,this };
            auto& transform = entity.GetComponent<TransformComponent>();
            auto& rb2d = entity.GetComponent<RigidBody2DComponent>();

            b2BodyDef bodyDef;
            bodyDef.type = RigidBodyTypeToBox2DBody(rb2d.Type);
            bodyDef.position.Set(transform.Translation.x, transform.Translation.y);
            bodyDef.angle = transform.Rotation.z;

            b2Body* body = m_PhysicsWorld->CreateBody(&bodyDef);
            body->SetFixedRotation(rb2d.FixedRotation);

            if (entity.HasComponent<BoxCollider2DComponent>())
            {
                auto& bc2d = entity.GetComponent<BoxCollider2DComponent>();
                b2PolygonShape boxShape;
                boxShape.SetAsBox(transform.Scale.x * bc2d.Size.x, transform.Scale.y * bc2d.Size.y);
                b2FixtureDef fixtureDef;
                fixtureDef.shape = &boxShape;
                fixtureDef.density = bc2d.Density;
                fixtureDef.friction = bc2d.Friction;
                fixtureDef.restitution = bc2d.Restitution;
                fixtureDef.restitutionThreshold = bc2d.RestitutionThreshold;
                body->CreateFixture(&fixtureDef);
            }

            if (entity.HasComponent<CircleCollider2DComponent>())
            {
                auto& cc2d = entity.GetComponent<CircleCollider2DComponent>();

                b2CircleShape circleShape;
                // 圆心偏移（局部空间，Box2D 单位与 Transform 一致）
                circleShape.m_p.Set(cc2d.Offset.x, cc2d.Offset.y);
                // 半径需乘以实体的均匀缩放（取 x/y 中的较大值以保持碰撞形状合理）
                circleShape.m_radius = glm::max(transform.Scale.x, transform.Scale.y) * cc2d.Radius;

                b2FixtureDef fixtureDef;
                fixtureDef.shape = &circleShape;
                fixtureDef.density = cc2d.Density;
                fixtureDef.friction = cc2d.Friction;
                fixtureDef.restitution = cc2d.Restitution;
                fixtureDef.restitutionThreshold = cc2d.RestitutionThreshold;
                body->CreateFixture(&fixtureDef);
            }

            rb2d.RuntimeBody = body;
        }
    }

    void Scene::OnPhysics2DStop()
    {
        delete m_PhysicsWorld;
        m_PhysicsWorld = nullptr;
    }

    template<typename T>
    void OnComponentAdded(Entity entity, T& component)
    {
       // static_assert(false);
    }

    template<>
    void Scene::OnComponentAdded<IDComponent>(Entity entity, IDComponent& component)
    {

    }

    template<>
    void Scene::OnComponentAdded<TransformComponent>(Entity entity, TransformComponent& component)
    {

    }

    template<>
    void Scene::OnComponentAdded<CameraComponent>(Entity entity, CameraComponent& component)
    {
        component.Camera.SetViewportSize(m_ViewportWidth, m_ViewportHeight);
    }

    template<>
    void Scene::OnComponentAdded<SpriteRendererComponent>(Entity entity, SpriteRendererComponent& component)
    {

    }

    template<>
    void Scene::OnComponentAdded<CircleRendererComponent>(Entity entity, CircleRendererComponent& component)
    {

    }

    template<>
    void Scene::OnComponentAdded<TagComponent>(Entity entity, TagComponent& component)
    {

    }

    template<>
    void Scene::OnComponentAdded<NativeScriptComponent>(Entity entity, NativeScriptComponent& component)
    {

    }

    template<>
    void Scene::OnComponentAdded<RigidBody2DComponent>(Entity entity, RigidBody2DComponent& component)
    {

    }

    template<>
    void Scene::OnComponentAdded<BoxCollider2DComponent>(Entity entity, BoxCollider2DComponent& component)
    {

    }

    template<>
    void Scene::OnComponentAdded<CircleCollider2DComponent>(Entity entity, CircleCollider2DComponent& component)
    {

    }

    template<>
    void Scene::OnComponentAdded<MeshRendererComponent>(Entity entity, MeshRendererComponent& component)
    {

    }

    template<>
    void Scene::OnComponentAdded<ModelRendererComponent>(Entity entity, ModelRendererComponent& component)
    {

    }

    template<>
    void Scene::OnComponentAdded<DirectionalLightComponent>(Entity entity, DirectionalLightComponent& component)
    {

    }

    template<>
    void Scene::OnComponentAdded<PointLightComponent>(Entity entity, PointLightComponent& component)
    {

    }

    template<>
    void Scene::OnComponentAdded<ParticleEmitterComponent>(Entity entity, ParticleEmitterComponent& component)
    {

    }

    template<>
    void Scene::OnComponentAdded<TextureAnimationComponent>(Entity entity, TextureAnimationComponent& component)
    {

    }

}