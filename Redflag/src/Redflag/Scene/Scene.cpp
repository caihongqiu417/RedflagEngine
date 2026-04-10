#include"rfpch.h"
#include"Scene.h"
#include"Renderer/Renderer2D.h"

#include"Entity.h"

//BOX2D
#include"box2d/b2_world.h"
#include"box2d/b2_fixture.h"
#include"box2d/b2_polygon_shape.h"


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

	Entity Scene::CreateEntity(const std::string& name)
	{
		Entity entity = {m_Registry.create(),this };

		entity.AddComponent<TransformComponent>();
		auto& tag=entity.AddComponent<TagComponent>();
		tag.Tag = name.empty() ? "Entity" : name;
		return entity;
	}

    void Scene::DestroyEntity(Entity entity)
    {
        m_Registry.destroy(entity);
    }

    void Scene::OnRuntimeStart()
    {
        m_PhysicsWorld = new b2World({ 0.f,-9.8f });
        auto view = m_Registry.view<RigidBody2DComponent>();
        for (auto e : view)
        {
            Entity entity = { e,this };
            auto& transform = entity.GetComponent<TransformComponent>();
            auto& rb2d = entity.GetComponent<RigidBody2DComponent>();

            //box2DBody接管当前transform
            b2BodyDef bodyDef;
            bodyDef.type = RigidBodyTypeToBox2DBody(rb2d.Type);
            bodyDef.position.Set(transform.Translation.x, transform.Translation.y);
            bodyDef.angle = transform.Rotation.z;

            b2Body* body=m_PhysicsWorld->CreateBody(&bodyDef);
            body->SetFixedRotation(rb2d.FixedRotation);

            if (entity.HasComponent<BoxCollider2DComponent>())
            {
                auto& bc2d = entity.GetComponent<BoxCollider2DComponent>();
                b2PolygonShape boxShape;
                boxShape.SetAsBox(transform.Scale.x*bc2d.Size.x, transform.Scale.y*bc2d.Size.y);

                //box2DBody接管当前碰撞参数
                b2FixtureDef fixtureDef;
                fixtureDef.shape = &boxShape;
                fixtureDef.density = bc2d.Density;
                fixtureDef.friction = bc2d.Friction;
                fixtureDef.restitution = bc2d.Restitution;
                fixtureDef.restitutionThreshold = bc2d.RestitutionThreshold;
                body->CreateFixture(&fixtureDef);
            }

            //保存到组件
            rb2d.RuntimeBody = body;
        }
    }

    void Scene::OnRuntimeStop()
    {
        delete m_PhysicsWorld;
        m_PhysicsWorld = nullptr;
    }

    //编辑器：用传入的编辑器相机
    void Scene::OnUpdateEditor(Timestep ts,EditorCamera& camera)
    {
        Renderer2D::BeginScene(camera);

        // 使用 view 和 each 来渲染所有 Sprite
        auto spriteView = m_Registry.view<TransformComponent, SpriteRendererComponent>();
        spriteView.each([&](auto entity, TransformComponent& transform, SpriteRendererComponent& sprite)
            {
                Renderer2D::DrawSprite(transform.GetTransform(), sprite, (int)entity);
            });

        Renderer2D::EndScene();
    }

    //runtime:从场景中找主相机
    void Scene::OnUpdateRuntime(Timestep ts)
    {   //===============================================================
        //=========================更新脚本==============================
        //===============================================================
        {
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

        // 物理
        // 脚本施加力/速度 → 根据力计算碰撞与位置 → 渲染同步变换。
        // 防止先物理后脚本导致输入延迟
        {
            const int32_t velocityIterations = 6;
            const int32_t positionIterations = 2;
            m_PhysicsWorld->Step(ts, velocityIterations, positionIterations);

            //从box2D获取变换
            auto view = m_Registry.view<RigidBody2DComponent>();
            for (auto e : view)
            {
                Entity entity = { e,this };
                auto& transform = entity.GetComponent<TransformComponent>();
                auto& rb2d = entity.GetComponent<RigidBody2DComponent>();
                
                // 取出之前存储并更新的刚体指针，用刚体指针将物理引擎的位置/角度同步回 Transform
                b2Body* body = (b2Body*)rb2d.RuntimeBody;
                const auto& position = body->GetPosition();
                transform.Translation.x = position.x;
                transform.Translation.y = position.y;
                transform.Rotation.z = body->GetAngle();
            }
        }

        
        //===============================================================
        //=========================找主相机==============================
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
       //=========================实际渲染==============================
       //===============================================================
        if (mainCamera)
        {
            Renderer2D::BeginScene(mainCamera->GetProjection(), cameraTransform);

            // 使用 view 和 each 来渲染所有 Sprite
            auto spriteView = m_Registry.view<TransformComponent, SpriteRendererComponent>();
            spriteView.each([&](auto entity, TransformComponent& transform, SpriteRendererComponent& sprite)
                {
                    Renderer2D::DrawSprite(transform.GetTransform(), sprite, (int)entity);
                });

            Renderer2D::EndScene();
        }
    }

    void Scene::OnViewportResize(uint32_t width, uint32_t height)
    {
        m_ViewportWidth = width;
        m_ViewportHeight = height;

        //将视口相机（AspectRatio不变） 调整大小
        auto view = m_Registry.view<CameraComponent>();
        for (auto entity : view)
        {
            auto& cameraComponent=view.get<CameraComponent>(entity);
            if (cameraComponent.FixedAspectRatio)
                cameraComponent.Camera.SetViewportSize(width, height);
        }
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

    template<typename T>
    void OnComponentAdded(Entity entity, T& component)
    {
        static_assert(false);
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
}