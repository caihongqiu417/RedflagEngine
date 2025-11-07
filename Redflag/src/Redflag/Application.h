#pragma once
#include "Core.h"

//test for event
#include"Events/Event.h"

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

