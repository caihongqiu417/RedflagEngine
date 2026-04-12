#include"rfpch.h"
#include"Scene.h"
#include"Renderer/Renderer2D.h"
#include"Renderer/Renderer3D.h"
#include"Redflag/Core/AssetManager.h"
#include"Redflag/Renderer/Model.h"

#include"Entity.h"
#include"ScriptableEntity.h"

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
        CopyComponent<CameraComponent>(dstSceneRegistry, srcSceneRegistry, enttMap);
        CopyComponent<NativeScriptComponent>(dstSceneRegistry, srcSceneRegistry, enttMap);
        CopyComponent<RigidBody2DComponent>(dstSceneRegistry, srcSceneRegistry, enttMap);
        CopyComponent<BoxCollider2DComponent>(dstSceneRegistry, srcSceneRegistry, enttMap);
        CopyComponent<MeshRendererComponent>(dstSceneRegistry, srcSceneRegistry, enttMap);
        CopyComponent<ModelRendererComponent>(dstSceneRegistry, srcSceneRegistry, enttMap);

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
        m_PhysicsWorld = new b2World({ 0.f,-9.8f });
        auto view = m_Registry.view<RigidBody2DComponent>();
        for (auto e : view)
        {
            Entity entity = { e,this };
            auto& transform = entity.GetComponent<TransformComponent>();
            auto& rb2d = entity.GetComponent<RigidBody2DComponent>();

            //box2DBody�ӹܵ�ǰtransform
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

                //box2DBody�ӹܵ�ǰ��ײ����
                b2FixtureDef fixtureDef;
                fixtureDef.shape = &boxShape;
                fixtureDef.density = bc2d.Density;
                fixtureDef.friction = bc2d.Friction;
                fixtureDef.restitution = bc2d.Restitution;
                fixtureDef.restitutionThreshold = bc2d.RestitutionThreshold;
                body->CreateFixture(&fixtureDef);
            }

            //���浽���
            rb2d.RuntimeBody = body;
        }
    }

    void Scene::OnRuntimeStop()
    {
        delete m_PhysicsWorld;
        m_PhysicsWorld = nullptr;
    }

    //�༭�����ô���ı༭�����
    void Scene::OnUpdateEditor(Timestep ts,EditorCamera& camera)
    {
        Renderer2D::BeginScene(camera);

        // ʹ�� view �� each ����Ⱦ���� Sprite
        auto spriteView = m_Registry.view<TransformComponent, SpriteRendererComponent>();
        spriteView.each([&](auto entity, TransformComponent& transform, SpriteRendererComponent& sprite)
            {
                Renderer2D::DrawSprite(transform.GetTransform(), sprite, (int)entity);
            });

        Renderer2D::EndScene();

        // 3D 渲染：MeshRendererComponent
        Renderer3D::BeginScene(camera);
        auto meshView = m_Registry.view<TransformComponent, MeshRendererComponent>();
        meshView.each([&](auto entity, TransformComponent& transform, MeshRendererComponent& mr)
            {
                if (mr.MeshData)
                {
                    if (mr.Texture)
                        Renderer3D::DrawMesh(mr.MeshData, transform.GetTransform(), mr.Texture, (int)entity);
                    else
                        Renderer3D::DrawMesh(mr.MeshData, transform.GetTransform(), mr.Color, (int)entity);
                }
            });

        // 3D 渲染：ModelRendererComponent
        auto modelView = m_Registry.view<TransformComponent, ModelRendererComponent>();
        modelView.each([&](auto entity, TransformComponent& transform, ModelRendererComponent& mr)
            {
                // 懒加载：路径有效但 ModelAsset 未加载时，通过 AssetManager 加载
                if (!mr.ModelAsset && !mr.FilePath.empty())
                    mr.ModelAsset = AssetManager::GetOrLoad<Model>(mr.FilePath);

                if (mr.ModelAsset)
                    Renderer3D::DrawModel(mr.ModelAsset, transform.GetTransform(), mr.Color, (int)entity);
            });

        Renderer3D::EndScene();
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
       //=========================ʵ����Ⱦ==============================
       //===============================================================
        if (mainCamera)
        {
            Renderer2D::BeginScene(mainCamera->GetProjection(), cameraTransform);

            // ʹ�� view �� each ����Ⱦ���� Sprite
            auto spriteView = m_Registry.view<TransformComponent, SpriteRendererComponent>();
            spriteView.each([&](auto entity, TransformComponent& transform, SpriteRendererComponent& sprite)
                {
                    Renderer2D::DrawSprite(transform.GetTransform(), sprite, (int)entity);
                });

            Renderer2D::EndScene();

            // 3D 运行时渲染：ModelRendererComponent
            glm::mat4 inverseTransform = glm::inverse(cameraTransform);
            glm::mat4 viewProj3D = mainCamera->GetProjection() * inverseTransform;
            Renderer3D::BeginScene(viewProj3D);
            auto modelView = m_Registry.view<TransformComponent, ModelRendererComponent>();
            modelView.each([&](auto entity, TransformComponent& transform, ModelRendererComponent& mr)
                {
                    if (!mr.ModelAsset && !mr.FilePath.empty())
                        mr.ModelAsset = AssetManager::GetOrLoad<Model>(mr.FilePath);

                    if (mr.ModelAsset)
                        Renderer3D::DrawModel(mr.ModelAsset, transform.GetTransform(), mr.Color, (int)entity);
                });
            Renderer3D::EndScene();
        }
    }

    void Scene::OnViewportResize(uint32_t width, uint32_t height)
    {
        m_ViewportWidth = width;
        m_ViewportHeight = height;

        //���ӿ������AspectRatio���䣩 ������С
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

        // CreateEntity ������ TransformComponent
        // ������ CopyComponentIfExist ���ǣ��ڲ��� ReplaceorEmplace��
        CopyComponentIfExist<TransformComponent>(newEntity, entity);
        CopyComponentIfExist<SpriteRendererComponent>(newEntity, entity);
        CopyComponentIfExist<CameraComponent>(newEntity, entity);
        CopyComponentIfExist<RigidBody2DComponent>(newEntity, entity);
        CopyComponentIfExist<BoxCollider2DComponent>(newEntity, entity);
        CopyComponentIfExist<NativeScriptComponent>(newEntity, entity);
        CopyComponentIfExist<MeshRendererComponent>(newEntity, entity);
        CopyComponentIfExist<ModelRendererComponent>(newEntity, entity);
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
    void Scene::OnComponentAdded<MeshRendererComponent>(Entity entity, MeshRendererComponent& component)
    {

    }

    template<>
    void Scene::OnComponentAdded<ModelRendererComponent>(Entity entity, ModelRendererComponent& component)
    {

    }

}