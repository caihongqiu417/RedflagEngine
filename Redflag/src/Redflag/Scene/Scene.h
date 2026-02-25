#pragma once

//负责存储所有entity数据
#include "entt.hpp"
#include"Components.h"
#include"Redflag/Core/Timestep.h"

namespace Redflag {

	class Scene
	{
	public:
		Scene();
		~Scene();

		entt::entity CreateEntity();

		//TEMP
		entt::registry& Reg() { return m_Rigistry; }

		void OnUpdate(Timestep ts);
	private:
		entt::registry m_Rigistry;
	};
}