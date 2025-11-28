#pragma once
#include "Core.h"

//test for event
#include"Events/Event.h"

#include<Redflag/Window.h>

namespace Redflag
{
	class REDFLAG_API Application
	{
	public:
		Application();
		virtual ~Application();

		void Run();

	private:
		std::unique_ptr<Window>  m_Window;
		bool m_Running = true;

	};

	Application* CreateApplication();
}

