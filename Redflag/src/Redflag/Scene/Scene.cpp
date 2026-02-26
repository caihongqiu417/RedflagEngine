#include"rfpch.h"
#include"Scene.h"
#include"Renderer/Renderer2D.h"

#include"Entity.h"

namespace Redflag {

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

    void Scene::OnUpdate(Timestep ts)
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
        //===============================================================
        //=========================找主相机==============================
        //===============================================================
        Camera* mainCamera = nullptr;
        glm::mat4* cameraTransform = nullptr;

        auto cameraView = m_Registry.view<TransformComponent, CameraComponent>();
        cameraView.each([&](auto entity, TransformComponent& transform, CameraComponent& camera) {
            if (camera.Primary)
            {
                mainCamera = &camera.Camera;
                cameraTransform = &transform.Transform;
            }
            });

        //===============================================================
       //=========================实际渲染==============================
       //===============================================================
        if (mainCamera)
        {
            Renderer2D::BeginScene(mainCamera->GetProjection(), *cameraTransform);

            // 使用 view 和 each 来渲染所有 Sprite
            auto spriteView = m_Registry.view<TransformComponent, SpriteRendererComponent>();
            spriteView.each([&](auto entity, TransformComponent& transform, SpriteRendererComponent& sprite) {
                Renderer2D::DrawQuad(transform.Transform, sprite.Color);
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
            if (!cameraComponent.FixedAspectRatio)
                cameraComponent.Camera.SetViewportSize(width, height);
        }
    }
}