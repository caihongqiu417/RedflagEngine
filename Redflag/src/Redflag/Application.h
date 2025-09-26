//应用基类和最小运行骨架
#pragma once
#include "Core.h"

namespace Redflag
{
	class REDFLAG_API Application
	{
	public:
		Application();
		virtual ~Application();

		void Run();
	};

	Application* CreateApplication();
}

