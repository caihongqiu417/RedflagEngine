#pragma once

#include "entt.hpp"
#include"Components.h"
#include"Redflag/Core/Timestep.h"


namespace Redflag {

	class Entity;

	class Scene
	{
	public:
		Scene();
		~Scene();

		Entity CreateEntity(const std::string& name=std::string());

		void OnUpdate(Timestep ts);
	private:
		entt::registry m_Registry;

		friend class Entity;
	};
}