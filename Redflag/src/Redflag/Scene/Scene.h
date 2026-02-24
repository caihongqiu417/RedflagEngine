#pragma once

#include"entt.hpp"

namespace Redflag {

	class Scene
	{
	public:
		Scene();
		~Scene();
	private:
		entt::registry m_Rigistry;
	};
}