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
	{
		//找到有两个组件的entity
		auto group =m_Registry.group<TransformComponent>(entt::get<SpriteRendererComponent>);
		for (auto entity : group)
		{
			//对于每个entity，将组件存储为元组
			auto& [transform, sprite] = group.get<TransformComponent, SpriteRendererComponent>(entity);

			Renderer2D::DrawQuad(transform.Transform, sprite.Color);
		}
	}
}