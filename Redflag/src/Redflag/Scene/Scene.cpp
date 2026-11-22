#include"rfpch.h"
#include"Scene.h"

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
        m_Registry.on_destroy<RigidBody2DComponent>().connect<&Scene::RemoveBody2D>(*this);
        m_Registry.on_destroy<BoxCollider2DComponent>().connect<&Scene::RemoveFixture2D<BoxCollider2DComponent>>(*this);
        m_Registry.on_destroy<CircleCollider2DComponent>().connect<&Scene::RemoveFixture2D<CircleCollider2DComponent>>(*this);
        m_Registry.on_update<BoxCollider2DComponent>().connect<&Scene::RemoveFixture2D<BoxCollider2DComponent>>(*this);
        m_Registry.on_update<CircleCollider2DComponent>().connect<&Scene::RemoveFixture2D<CircleCollider2DComponent>>(*this);
        m_Registry.on_destroy<RigidBody3DComponent>().connect<&Scene::RemovePhysicsBody>(*this);
        m_Registry.on_destroy<BoxCollider3DComponent>().connect<&Scene::RemovePhysicsBody>(*this);
	}

	Scene::~Scene()
	{
        m_Registry.on_destroy<RigidBody2DComponent>().disconnect(this);
        m_Registry.on_destroy<BoxCollider2DComponent>().disconnect(this);
        m_Registry.on_destroy<CircleCollider2DComponent>().disconnect(this);
        m_Registry.on_update<BoxCollider2DComponent>().disconnect(this);
        m_Registry.on_update<CircleCollider2DComponent>().disconnect(this);
        m_Registry.on_destroy<RigidBody3DComponent>().disconnect(this);
        m_Registry.on_destroy<BoxCollider3DComponent>().disconnect(this);
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
                dst.emplace_or_replace<Component>(dstEnttID, src.get<Component>(e));
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
                dst.AddOrReplaceComponent<Component>(src.GetComponent<Component>());
        }
    }

    Ref<Scene> Scene::Copy(Ref<Scene> other)
    {
        Ref<Scene> newScene = CreateRef<Scene>();

        newScene->m_ViewportWidth = other->m_ViewportWidth;
        newScene->m_ViewportHeight = other->m_ViewportHeight;
        newScene->m_Environment = other->m_Environment;

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

        //UUID->entity映射表，因为C++和C#之间传递uuid
        m_EntityMap[uuid] = entity;

        return entity;
    }

    void Scene::DestroyEntity(Entity entity)
    {
		UUID entityID = entity.GetUUID();
		ScriptEngine::OnDestroyEntity(entityID);
        m_Registry.destroy(entity);
		m_EntityMap.erase(entityID);
    }

    void Scene::OnRuntimeStart()
    {
        OnPhysics2DStart();
        SyncPhysics3D();

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

    void Scene::OnRuntimeStop()
    {
        StopPhysics3D();
        OnPhysics2DStop();

        ScriptEngine::OnRuntimeStop();

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
        SyncPhysics3D();
    }

    void Scene::OnSimulationStop()
    {
        StopPhysics3D();
        OnPhysics2DStop();
    }

    void Scene::OnUpdateEditor(Timestep ts,EditorCamera& camera)
    {
        SceneRenderer::RenderScene(*this, camera);
    }

    //runtime
    void Scene::OnUpdateRuntime(Timestep ts)
    {   // ----------
        // Scripts
        // ----------
        {
            auto view = m_Registry.view<ScriptComponent>();
            for (auto e : view)
            {
                Entity entity = { e,this };
                ScriptEngine::OnUpdateEntity(entity, ts);
            }

            m_Registry.view<NativeScriptComponent>().each([=](auto entity, auto& nsc)
            {
                    // entity: 当前遍历到的实体
                     // nsc: 这个实体上的 NativeScriptComponent
                if (!nsc.Instance)
                {
                    nsc.Instance = nsc.InstantiateScript();
                    nsc.Instance->m_Entity = Entity{ entity,this };
                    nsc.Instance->OnCreate();
                }
                nsc.Instance->OnUpdate(ts);
            });
        }

        // ----------
        // 更新粒子发射器
        // ----------
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

        OnUpdatePhysics(ts);

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

    void Scene::OnUpdateSimulation(Timestep ts, EditorCamera& camera)
    {
        OnUpdatePhysics(ts);
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

        newEntity.GetComponent<TransformComponent>() = entity.GetComponent<TransformComponent>();
        CopyComponentIfExist<SpriteRendererComponent>(newEntity, entity);
        CopyComponentIfExist<CircleRendererComponent>(newEntity, entity);
        CopyComponentIfExist<CameraComponent>(newEntity, entity);
        CopyComponentIfExist<RigidBody2DComponent>(newEntity, entity);
        CopyComponentIfExist<BoxCollider2DComponent>(newEntity, entity);
        CopyComponentIfExist<RigidBody3DComponent>(newEntity, entity);
        CopyComponentIfExist<BoxCollider3DComponent>(newEntity, entity);
        CopyComponentIfExist<PickupComponent>(newEntity, entity);
        CopyComponentIfExist<CircleCollider2DComponent>(newEntity, entity);
        CopyComponentIfExist<NativeScriptComponent>(newEntity, entity);
        CopyComponentIfExist<ScriptComponent>(newEntity, entity);
		if (newEntity.HasComponent<ScriptComponent>())
			ScriptEngine::GetScriptFieldCache(newEntity.GetUUID()) = ScriptEngine::GetScriptFieldCache(entity.GetUUID());
        CopyComponentIfExist<MeshRendererComponent>(newEntity, entity);
        if (newEntity.HasComponent<MeshRendererComponent>())
        {
            auto& mrc = newEntity.GetComponent<MeshRendererComponent>();
            if (mrc.Mat)
                mrc.Mat = mrc.Mat->Clone();
        }

        CopyComponentIfExist<ModelRendererComponent>(newEntity, entity);
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
        if (m_Boxes3D.empty())
            return;

        m_Physics3D->Step(ts.GetSeconds());
        for (const auto& [handle, box] : m_Boxes3D)
        {
            if (box.Type != BodyType3D::Dynamic)
                continue;
            TransformComponent& transform = m_Registry.get<TransformComponent>(handle);
            glm::quat rotation;
            if (m_Physics3D->GetPose(box.Entity, transform.Translation, rotation))
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
                glm::abs(transform.Scale) * collider.HalfSize, rigidBody.Density };

            // 压扁到零厚度的盒子没有体积。恢复尺寸后，下次物理步会重新加入它。
            if (!glm::all(glm::greaterThan(box.HalfSize, glm::vec3(0))) ||
                (box.Type != BodyType3D::Static && !(box.Density > 0.f)) ||
                m_Registry.any_of<RigidBody2DComponent>(handle))
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
                if (old.Type == box.Type && old.HalfSize == box.HalfSize && old.Density == box.Density &&
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
        m_Boxes3D.clear();
        m_Physics3D.reset();
    }

    std::optional<RayHit3D> Scene::Raycast(glm::vec3 origin, glm::vec3 direction, float maxDistance) const
    {
        if (!m_Physics3D)
            return std::nullopt;
        return m_Physics3D->Raycast(origin, direction, maxDistance);
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
