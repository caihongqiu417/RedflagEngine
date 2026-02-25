#include"rfpch.h"
#include"Scene.h"
#include"Renderer/Renderer2D.h"

namespace Redflag {

	Scene::Scene()
	{

	}

	Scene::~Scene()
	{

	}
	entt::entity Scene::CreateEntity()
	{
		return m_Rigistry.create();
	}

	void Scene::OnUpdate(Timestep ts)
	{
		//找到有两个组件的entity
		auto group = m_Rigistry.group<TransformComponent>(entt::get<SpriteRendererComponent>);
		for (auto entity : group)
		{
			//对于每个entity，将组件存储为元组
			auto& [transform, sprite] = group.get<TransformComponent, SpriteRendererComponent>(entity);

			Renderer2D::DrawQuad(transform.Transform, sprite.Color);
		}
	}
}