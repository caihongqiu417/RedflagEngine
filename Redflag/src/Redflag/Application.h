#pragma once
#include "Core.h"

//test for event
#include"Events/Event.h"

#include<Redflag/Window.h>

#include"Redflag/LayerStack.h"

#include"Redflag/Events/Event.h"
#include "Redflag/Events/ApplicationEvent.h" 



namespace Redflag
{
	class REDFLAG_API Application
	{
	public:
		Application();
		virtual ~Application();

		void Run();

		void OnEvent(Event& e);

		void PushLayer(Layer* layer);
		void PushOverlayer(Layer* overlay);

		bool OnWindowClose(WindowCloseEvent& e);
	
	private:
		std::unique_ptr<Window>  m_Window;
		bool m_Running = true;
		LayerStack m_LayerStack;

	};

	Application* CreateApplication();
}

