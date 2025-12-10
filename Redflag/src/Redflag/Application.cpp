#include "rfpch.h"
#include "Application.h"
#include "Redflag/Log.h"

//test for event
#include "Events/ApplicationEvent.h"
#include"glad/glad.h"
#include<GLFW/glfw3.h>
#include "imgui.h"


namespace Redflag
{
	#define BIND_EVENT_FN(x) std::bind(&Application::x,this,std::placeholders::_1)

	Application::Application() {
		m_Window = std::unique_ptr<Window>(Window::Create());

		//把成员函数onEvent存储到m_window的m_data中
		m_Window->SetEventCallback(BIND_EVENT_FN(Application::OnEvent));
	}
	Application::~Application() 
	{

	}

	void Application::PushLayer(Layer* layer)
	{
		m_LayerStack.PushLayer(layer);
	}

	void Application::PushOverlayer(Layer* layer)
	{
		m_LayerStack.PushOverlay(layer);
	}

	void Application::OnEvent(Event& e)
	{
		EventDispatcher dispatcher(e);
		dispatcher.Dispatch<WindowCloseEvent>(BIND_EVENT_FN(OnWindowClose));
		RF_CORE_INFO("{0}", e.ToString());

		//从overlay从后往前传播事件
		for (auto it = m_LayerStack.end();it != m_LayerStack.begin();)
		{
			(*--it)->OnEvent(e);
			if (e.Handled())
				break;
		}
	}


	void Application::Run() {
		WindowResizeEvent e(1280, 720);
		if (e.IsInCategory(EventCategoryApplication)) {
			RF_CORE_TRACE("Window resize event triggered"); 
		}
		if (e.IsInCategory(EventCategoryInput)) {
			RF_CORE_ERROR("Event category check: Window resize should not be in input category"); 
		}

		while (m_Running)
		{
			glClearColor(1, 0, 1, 1);
			glClear(GL_COLOR_BUFFER_BIT);
			
			for (Layer* layer : m_LayerStack)
				layer->OnUpdate();
			
			m_Window->OnUpdate();
		}
	}

	bool Application::OnWindowClose(WindowCloseEvent& e)
	{
		m_Running = false;
		return true;
	}

} 